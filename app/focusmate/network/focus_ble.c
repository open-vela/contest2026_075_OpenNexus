/* SPDX-License-Identifier: Apache-2.0
 * FocusMate BLE bring-up: real board HCI -> Zephyr GATT -> iPhone relay.
 * Diagnostic text only. This profile neither uploads microphone data nor
 * embeds API keys, and does not claim that a BLE connection is Internet access.
 */
#include <nuttx/config.h>
#include <nuttx/cache.h>
#include <nuttx/mm/mm.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/uuid.h>
#include <netutils/cJSON.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>
#include <unistd.h>
#include <time.h>
#include "ble_frame.h"

#define SERVICE_BYTES BT_UUID_128_ENCODE(0x6e400001,0xb5a3,0xf393,0xe0a9,0xe50e24dcca9eULL)
#define RX_UUID BT_UUID_DECLARE_128(BT_UUID_128_ENCODE(0x6e400002,0xb5a3,0xf393,0xe0a9,0xe50e24dcca9eULL))
#define TX_UUID BT_UUID_DECLARE_128(BT_UUID_128_ENCODE(0x6e400003,0xb5a3,0xf393,0xe0a9,0xe50e24dcca9eULL))
#define QUEUE_DEPTH 4
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static struct {
  int pid,error;
  bool wanted,host_ready,advertising,subscribed,cloud_requested,pending,network_reported;
  struct bt_conn *conn;
  unsigned head,count,rx,tx,generation,advertise_failures;
  uint64_t deadline;
  char request_id[40];
  char queue[QUEUE_DEPTH][FM_BLE_LINE_MAX];
  struct fm_ble_frame frame;
  unsigned loops,processed,injected;
} state;
extern int focus_ble_hci_probe(void);
extern void focus_ble_hci_stats(void);
static uint64_t clock_ms(void)
{
  struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);
  return (uint64_t)t.tv_sec*1000+t.tv_nsec/1000000;
}
static int queue_line(const char *line,size_t n,void *arg)
{
  (void)arg; /* Caller holds lock; Bluetooth callbacks never do HTTP/JSON work. */
  if(state.count==QUEUE_DEPTH)return -ENOSPC;
  unsigned slot=(state.head+state.count)%QUEUE_DEPTH;
  memcpy(state.queue[slot],line,n);state.queue[slot][n]=0;
  state.count++;state.rx++;return 0;
}
static ssize_t write_rx(struct bt_conn *conn,const struct bt_gatt_attr *attr,
                        const void *buf,uint16_t len,uint16_t offset,uint8_t flags)
{
  (void)conn;(void)attr;
  if(offset || (flags&BT_GATT_WRITE_FLAG_PREPARE))return BT_GATT_ERR(BT_ATT_ERR_NOT_SUPPORTED);
  if(!len)return 0;
  pthread_mutex_lock(&lock);
  int ret;
  if(!state.frame.used && !state.frame.dropping && len<=20 &&
     ((len==4 && !memcmp(buf,"ping",4)) || (len==4 && !memcmp(buf,"info",4))))
    ret=queue_line(buf,len,NULL);
  else ret=fm_ble_frame_feed(&state.frame,buf,len,queue_line,NULL);
  pthread_mutex_unlock(&lock);
  if(ret<0)return BT_GATT_ERR(BT_ATT_ERR_INSUFFICIENT_RESOURCES);
  return len;
}
static void ccc_changed(const struct bt_gatt_attr *attr,uint16_t value)
{
  (void)attr;pthread_mutex_lock(&lock);state.subscribed=(value==BT_GATT_CCC_NOTIFY);
  pthread_mutex_unlock(&lock);
  printf("[ble] notifications=%d\n",value==BT_GATT_CCC_NOTIFY);fflush(stdout);
}
static const char identity[]="FocusMate BLE v1";
static ssize_t read_tx(struct bt_conn *conn,const struct bt_gatt_attr *attr,
                       void *buf,uint16_t len,uint16_t offset)
{
  return bt_gatt_attr_read(conn,attr,buf,len,offset,identity,sizeof(identity)-1);
}
static struct bt_gatt_attr attrs[]={
  BT_GATT_PRIMARY_SERVICE(BT_UUID_DECLARE_128(SERVICE_BYTES)),
  BT_GATT_CHARACTERISTIC(RX_UUID,BT_GATT_CHRC_WRITE,BT_GATT_PERM_WRITE,NULL,write_rx,NULL),
  BT_GATT_CHARACTERISTIC(TX_UUID,BT_GATT_CHRC_READ|BT_GATT_CHRC_NOTIFY,
                         BT_GATT_PERM_READ,read_tx,NULL,NULL),
  BT_GATT_CCC(ccc_changed,BT_GATT_PERM_READ|BT_GATT_PERM_WRITE),
};
static struct bt_gatt_service service=BT_GATT_SERVICE(attrs);
static const struct bt_data ad[]={
  BT_DATA_BYTES(BT_DATA_FLAGS,BT_LE_AD_GENERAL|BT_LE_AD_NO_BREDR),
  BT_DATA_BYTES(BT_DATA_UUID128_ALL,SERVICE_BYTES),
};
static void connected(struct bt_conn *conn,uint8_t error)
{
  if(error){printf("[ble] connection failed=%u\n",error);fflush(stdout);return;}
  pthread_mutex_lock(&lock);
  state.conn=bt_conn_ref(conn);state.advertising=false;state.network_reported=false;
  pthread_mutex_unlock(&lock);
  puts("[ble] iPhone/central connected; Internet not yet verified");fflush(stdout);
}
static void disconnected(struct bt_conn *conn,uint8_t reason)
{
  struct bt_conn *old=NULL;
  pthread_mutex_lock(&lock);
  if(state.conn==conn){old=state.conn;state.conn=NULL;}
  state.subscribed=false;state.pending=false;state.network_reported=false;
  state.count=0;state.cloud_requested=false;fm_ble_frame_reset(&state.frame);
  pthread_mutex_unlock(&lock);
  if(old)bt_conn_unref(old);
  printf("[ble] disconnected reason=%u; pending request cancelled\n",reason);fflush(stdout);
}
static struct bt_conn_cb callbacks={.connected=connected,.disconnected=disconnected};
static int transmit(const char *text)
{
  pthread_mutex_lock(&lock);
  struct bt_conn *conn=(state.conn && state.subscribed)?bt_conn_ref(state.conn):NULL;
  pthread_mutex_unlock(&lock);
  if(!conn){pthread_mutex_lock(&lock);state.error=-ENOTCONN;pthread_mutex_unlock(&lock);puts("[ble-tx] no subscribed connection");return -ENOTCONN;}
  size_t length=strlen(text),offset=0;
  int ret=0;
  while(offset<length) {
    size_t n=length-offset;if(n>20)n=20; /* Works at the default ATT MTU 23. */
    uint64_t until=clock_ms()+2000;
    do {
      ret=bt_gatt_notify(conn,&attrs[4],text+offset,n);
      if(ret!=-ENOMEM && ret!=-EAGAIN)break;
      usleep(20000);
    }while(clock_ms()<until);
    if(ret)break;
    offset+=n;usleep(5000);
  }
  bt_conn_unref(conn);
  pthread_mutex_lock(&lock);
  if(!ret)state.tx++;else state.error=ret;
  pthread_mutex_unlock(&lock);
  return ret;
}
static void cloud_request(void)
{
  char wire[300],id[40];
  pthread_mutex_lock(&lock);
  if(state.pending){pthread_mutex_unlock(&lock);return;}
  snprintf(id,sizeof(id),"fm-%08lx-%u",(unsigned long)clock_ms(),++state.generation);
  snprintf(state.request_id,sizeof(state.request_id),"%s",id);
  state.pending=true;state.network_reported=false;state.deadline=clock_ms()+15000;
  pthread_mutex_unlock(&lock);
  snprintf(wire,sizeof(wire),"{\"v\":1,\"type\":\"net.request\",\"id\":\"%s\",\"url\":\"https://httpbin.org/get?focusmate_id=%s\"}\n",id,id);
  int ret=transmit(wire);
  if(ret){pthread_mutex_lock(&lock);state.pending=false;state.error=ret;pthread_mutex_unlock(&lock);}
  printf("[ble-net] HTTPS request id=%s queued=%d\n",id,ret==0);fflush(stdout);
}
static const char *json_string(cJSON *j,const char *key)
{
  cJSON *v=cJSON_GetObjectItemCaseSensitive(j,key);return cJSON_IsString(v)?v->valuestring:NULL;
}
static void process_line(const char *line)
{
  printf("[ble-app] dispatch=%s\n",!strcmp(line,"ping")?"ping":"other");fflush(stdout);
  if(!strcmp(line,"ping")){transmit("{\"type\":\"pong\"}\n");return;}
  if(!strcmp(line,"info")){transmit("{\"device\":\"FocusMate\",\"v\":1}\n");return;}
  cJSON *j=cJSON_ParseWithOpts(line,NULL,true);
  if(!j){transmit("{\"type\":\"error\",\"error\":\"invalid_json\"}\n");return;}
  const char *type=json_string(j,"type");
  if(type && !strcmp(type,"net.test"))cloud_request();
  else if(type && !strcmp(type,"ping"))transmit("{\"type\":\"pong\"}\n");
  else if(type && !strcmp(type,"net.reply")) {
    const char *id=json_string(j,"id"),*echo=json_string(j,"echo_id");
    cJSON *http=cJSON_GetObjectItemCaseSensitive(j,"http_status");
    pthread_mutex_lock(&lock);
    bool matched=state.pending && id && !strcmp(id,state.request_id);
    bool ok=matched && echo && !strcmp(echo,state.request_id) &&
            cJSON_IsNumber(http) && http->valueint==200;
    if(matched){state.pending=false;state.network_reported=ok;state.error=ok?0:-EHOSTUNREACH;}
    pthread_mutex_unlock(&lock);
    printf("[ble-net] phone response id=%s matched=%d HTTPS_echo_match=%d\n",id?id:"missing",matched,ok);fflush(stdout);
    if(matched) {
      char ack[120];
      snprintf(ack,sizeof(ack),"{\"type\":\"net.ack\",\"id\":\"%s\",\"ok\":%s}\n",id,ok?"true":"false");
      transmit(ack);
    }
  } else transmit("{\"type\":\"error\",\"error\":\"unsupported\"}\n");
  cJSON_Delete(j);
}
static void print_status(void)
{
  pthread_mutex_lock(&lock);
  int pid=state.pid,error=state.error;
  bool host=state.host_ready,adv=state.advertising,linked=state.conn!=NULL,
       notify=state.subscribed,pending=state.pending,network=state.network_reported;
  unsigned rx=state.rx,tx=state.tx;
  pthread_mutex_unlock(&lock);
  printf("[ble-status] daemon=%d host=%d advertising=%d connected=%d notify=%d rx=%u tx=%u pending=%d phone_https_report=%d error=%d\n",
         pid,host,adv,linked,notify,rx,tx,pending,network,error);
  pthread_mutex_lock(&lock);
  printf("[ble-worker] loops=%u processed=%u queued=%u head=%u injected=%u\n",state.loops,state.processed,state.count,state.head,state.injected);
  pthread_mutex_unlock(&lock);
  focus_ble_hci_stats();fflush(stdout);
}
static int ble_daemon(int argc,char **argv)
{
  (void)argc;(void)argv;
  struct bt_le_adv_param param=*BT_LE_ADV_CONN_NAME;
  param.options|=BT_LE_ADV_OPT_ONE_TIME; /* Application owns restart. */
  int ret=bt_enable(NULL);
  /* This multi-controller port allocates bt_dev in bt_enable_mc(). Its
   * callback list is unavailable before enable, unlike the global API in
   * upstream single-controller examples. Register before advertising. */
  if(!ret)ret=bt_conn_cb_register(&callbacks);
  if(!ret)ret=bt_set_name("FocusMate");
  if(!ret)ret=bt_gatt_service_register(&service);
  pthread_mutex_lock(&lock);state.host_ready=ret==0;state.error=ret;
  pthread_mutex_unlock(&lock);
  printf("[ble] host/service init result=%d\n",ret);fflush(stdout);
  for(;;) {
    char line[FM_BLE_LINE_MAX];line[0]=0;
    pthread_mutex_lock(&lock);
    state.loops++;
    bool wanted=state.wanted,adv=state.advertising,linked=state.conn!=NULL;
    bool request=state.cloud_requested;state.cloud_requested=false;
    struct bt_conn *closing=(!wanted && state.conn)?bt_conn_ref(state.conn):NULL;
    if(state.count){memcpy(line,state.queue[state.head],sizeof(line));state.head=(state.head+1)%QUEUE_DEPTH;state.count--;state.processed++;}
    if(state.pending && clock_ms()>state.deadline){state.pending=false;state.error=-ETIMEDOUT;puts("[ble-net] HTTPS reply timed out");}
    pthread_mutex_unlock(&lock);
    if(!ret && wanted && !adv && !linked) {
      int r=bt_le_adv_start(&param,ad,sizeof(ad)/sizeof(ad[0]),NULL,0);
      /* The MC port retains its legacy advertising object across a link.
       * Delete it before creating a fresh instance after disconnection. */
      if(r==-EALREADY) {
        r=bt_le_adv_stop();
        if(!r)r=bt_le_adv_start(&param,ad,sizeof(ad)/sizeof(ad[0]),NULL,0);
      }
      pthread_mutex_lock(&lock);state.advertising=r==0;state.error=r;
      if(r){if(++state.advertise_failures>=3)state.wanted=false;}
      else state.advertise_failures=0;
      pthread_mutex_unlock(&lock);
      printf("[ble] advertising FocusMate legacy/NUS result=%d\n",r);fflush(stdout);
      if(r)usleep(1000000);
    }
    if(!wanted && (adv || linked)){bt_le_adv_stop();pthread_mutex_lock(&lock);state.advertising=false;pthread_mutex_unlock(&lock);}
    if(closing){bt_conn_disconnect(closing,BT_HCI_ERR_REMOTE_USER_TERM_CONN);bt_conn_unref(closing);}
    if(line[0])process_line(line);
    if(request)cloud_request();
    usleep(50000);
  }
  return 0;
}
int main(int argc,char **argv)
{
  if(argc!=2){puts("focusmate_ble probe|start|stop|status|heap|inject|cloud");return 1;}
  if(!strcmp(argv[1],"nocache")){
    pthread_mutex_lock(&lock);bool busy=state.pid!=0;pthread_mutex_unlock(&lock);
    if(busy){puts("[ble-test] reboot first; host already started");return 1;}
#ifdef CONFIG_ARMV8M_DCACHE
    puts("[ble-test] runtime cache switching disabled; use boot-uncached profile");return 1;
#else
    puts("[ble-test] D-cache disabled at boot; no runtime switch");return 0;
#endif
  }

  if(!strcmp(argv[1],"heap")){
#ifdef CONFIG_DEBUG_MM
    umm_checkcorruption();puts("[ble-memory] heap integrity check passed");return 0;
#else
    puts("[ble-memory] CONFIG_DEBUG_MM is required for this check");return 1;
#endif
  }
  if(!strcmp(argv[1],"inject")){pthread_mutex_lock(&lock);int r=queue_line("ping",4,NULL);if(!r){state.rx--;state.injected++;}pthread_mutex_unlock(&lock);printf("[ble-test] local injection=%d; not BLE input\n",r);return r?1:0;}
  if(!strcmp(argv[1],"status")){print_status();return 0;}
  if(!strcmp(argv[1],"probe")){
    pthread_mutex_lock(&lock);bool busy=state.pid!=0;pthread_mutex_unlock(&lock);
    if(busy){puts("[ble-hci] daemon owns HCI; reset board before raw probe");return 1;}
    return focus_ble_hci_probe()<0?1:0;
  }
  pthread_mutex_lock(&lock);
  if(!strcmp(argv[1],"start")){
    state.wanted=true;state.advertise_failures=0;
    if(!state.pid){state.pid=-1;pthread_mutex_unlock(&lock);
      int pid=task_create("focusmate_ble",100,16384,ble_daemon,NULL);
      pthread_mutex_lock(&lock);state.pid=pid<0?0:pid;if(pid<0)state.error=-errno;
    }
  }else if(!strcmp(argv[1],"stop"))state.wanted=false;
  else if(!strcmp(argv[1],"cloud")){
    if(!state.conn || !state.subscribed){pthread_mutex_unlock(&lock);puts("[ble] connect and subscribe first");return 1;}
    state.cloud_requested=true;
  }else{pthread_mutex_unlock(&lock);return 1;}
  pthread_mutex_unlock(&lock);print_status();return 0;
}
