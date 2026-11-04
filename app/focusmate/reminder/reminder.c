/* SPDX-License-Identifier: Apache-2.0
 * Step 1: native cron -> local execution adapter -> UI-owned POSIX queue.
 * This is a scheduler integration test, not the complete LLM/Skill chain.
 */
#include <nuttx/config.h>
#include "infra/cron_service.h"
#include "core/message_bus.h"
#include "reminder.h"
#include "cJSON.h"
#include <mqueue.h>
#include <fcntl.h>
#include <sched.h>
#include <sys/stat.h>
#include <pthread.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

int fm_cron_init(void),fm_cron_start(void);
int fm_cron_add(cron_job_t *),fm_cron_remove(const char *);
int fm_cron_list(cron_job_t *,int);
static pthread_mutex_t lifecycle=PTHREAD_MUTEX_INITIALIZER;
static volatile int daemon_pid,ready,init_error;
static const char *store="/data/focusmate/reminder-cron.json";

/* Adapter used only by the isolated cron instance, not a fake global registry. */
int fm_reminder_execute(const char *name,const char *args,char *out,size_t cap)
{
  if(strcmp(name,"focusmate_prompt"))return -ENOSYS;
  cJSON *j=cJSON_Parse(args);
  const cJSON *v=j?cJSON_GetObjectItem(j,"title"):NULL;
  if(!cJSON_IsString(v)||!v->valuestring[0]||strlen(v->valuestring)>=FM_REMINDER_TITLE){cJSON_Delete(j);return -EINVAL;}
  struct fm_reminder_event e={.version=1};
  static unsigned sequence;
  struct timespec stamp;clock_gettime(CLOCK_MONOTONIC,&stamp);
  snprintf(e.key,sizeof(e.key),"%08lx%08x",(unsigned long)(uint32_t)stamp.tv_sec,++sequence);
  char path[128],title[FM_REMINDER_TITLE];snprintf(title,sizeof(title),"%s",v->valuestring);
  snprintf(path,sizeof(path),FM_REMINDER_EVENT_PATH,e.key);
  char *body=cJSON_PrintUnformatted(j);cJSON_Delete(j);
  if(!body)return -ENOMEM;
  FILE *fp=fopen(path,"w");size_t length=strlen(body);
  bool written=fp&&fwrite(body,1,length,fp)==length;
  if(fp&&fclose(fp)!=0)written=false;
  free(body);
  if(!written){unlink(path);snprintf(out,cap,"event save failed");return -EIO;}
  mqd_t q=mq_open(FM_REMINDER_QUEUE,O_WRONLY|O_NONBLOCK);
  if(q==(mqd_t)-1){int error=errno;unlink(path);snprintf(out,cap,"UI unavailable: %d",error);return -error;}
  int r=mq_send(q,(char *)&e,sizeof(e),0);int saved=errno;mq_close(q);
  if(r<0){unlink(path);snprintf(out,cap,"UI queue failed: %d",saved);return -saved;}
  printf("[reminder] FIRED epoch=%lld title=%s queue=delivered\n",(long long)time(NULL),title);fflush(stdout);
  snprintf(out,cap,"UI event queued");return 0;
}
int fm_reminder_outbound(const agent_msg_t *msg)
{
  /* UI execution is performed above. Consume only the diagnostic ack. */
  free(msg->content);free(msg->image_b64);return 0;
}
static int daemon_main(int argc,char **argv)
{
  (void)argc;(void)argv;
  int r=fm_cron_init();if(!r)r=fm_cron_start();
  init_error=r;ready=r? -1:1;
  printf("[reminder] scheduler ready=%d engine=ai_agent/cron_service poll=1s\n",ready);fflush(stdout);
  if(r){daemon_pid=0;return 1;}
  for(;;)sleep(1); /* Keep the native task group and cron pthread alive. */
  return 0;
}
static int ensure_started(void)
{
  pthread_mutex_lock(&lifecycle);
  if(!daemon_pid){
    mkdir("/data",0755);mkdir("/data/focusmate",0755);
    ready=0;init_error=0;
    daemon_pid=task_create("reminder_service",100,8192,daemon_main,NULL);
    if(daemon_pid<0){daemon_pid=0;pthread_mutex_unlock(&lifecycle);return -errno;}
  }
  pthread_mutex_unlock(&lifecycle);
  for(int i=0;i<100&&!ready;i++)usleep(20000);
  return ready==1?0:(init_error?init_error:-ETIMEDOUT);
}
static int saved_job(const char *id)
{
  char text[8193];FILE *fp=fopen(store,"r");if(!fp)return 0;
  size_t n=fread(text,1,sizeof(text)-1,fp);text[n]=0;fclose(fp);
  cJSON *root=cJSON_Parse(text),*item;int found=0;
  cJSON_ArrayForEach(item,cJSON_GetObjectItem(root,"jobs")){
    cJSON *key=cJSON_GetObjectItem(item,"id");
    if(cJSON_IsString(key)&&!strcmp(key->valuestring,id))found=1;
  }
  cJSON_Delete(root);return found;
}
int main(int argc,char **argv)
{
  if(argc<2){puts("focusmate_remind after <seconds> <title> | list | cancel <id>");return 1;}
  bool add=!strcmp(argv[1],"after"),list=!strcmp(argv[1],"list"),cancel=!strcmp(argv[1],"cancel");
  if((add&&argc!=4)||(list&&argc!=2)||(cancel&&argc!=3)||(!add&&!list&&!cancel))return 1;
  char *end=NULL;long delay=add?strtol(argv[2],&end,10):0;
  if(add&&(!end||*end||delay<1||delay>86400||!argv[3][0]||strlen(argv[3])>=FM_REMINDER_TITLE)){puts("[reminder] invalid delay/title");return 1;}
  if(add){mqd_t q=mq_open(FM_REMINDER_QUEUE,O_WRONLY|O_NONBLOCK);if(q==(mqd_t)-1){puts("[reminder] UI must be running; nothing scheduled");return 1;}mq_close(q);}
  int ret=ensure_started();if(ret){printf("[reminder] init error=%d\n",ret);return 1;}
  if(cancel){ret=fm_cron_remove(argv[2]);printf("[reminder] cancel id=%s result=%d\n",argv[2],ret);return ret?1:0;}
  cron_job_t jobs[16];int count=fm_cron_list(jobs,16);
  if(list){printf("[reminder] jobs=%d now=%lld\n",count,(long long)time(NULL));for(int i=0;i<count;i++)printf("[reminder] id=%s enabled=%d due=%lld title=%s\n",jobs[i].id,jobs[i].enabled,(long long)jobs[i].next_run,jobs[i].message);return 0;}
  for(int i=0;i<count;i++)if(jobs[i].enabled){puts("[reminder] one pending appointment allowed in step 1; cancel it first");return 1;}
  cron_job_t job={0};job.kind=CRON_KIND_AT;job.at_epoch=time(NULL)+delay;job.delete_after_run=true;
  snprintf(job.name,sizeof(job.name),"focusmate-once");snprintf(job.message,sizeof(job.message),"%s",argv[3]);
  snprintf(job.action,sizeof(job.action),"focusmate_prompt");snprintf(job.channel,sizeof(job.channel),"system");
  cJSON *j=cJSON_CreateObject();cJSON_AddStringToObject(j,"title",argv[3]);char *args=cJSON_PrintUnformatted(j);cJSON_Delete(j);
  if(!args||strlen(args)>=sizeof(job.action_args)){free(args);puts("[reminder] encoded title too long");return 1;}
  strcpy(job.action_args,args);free(args);
  ret=fm_cron_add(&job);
  if(ret||!saved_job(job.id)){if(!ret)fm_cron_remove(job.id);puts("[reminder] save failed; appointment not confirmed");return 1;}
  printf("[reminder] SCHEDULED id=%s delay=%ld due=%lld persisted=1 title=%s\n",job.id,delay,(long long)job.next_run,argv[3]);fflush(stdout);
  return 0;
}
