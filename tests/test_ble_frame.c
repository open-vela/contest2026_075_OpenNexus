#include "ble_frame.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
struct received { unsigned count;char line[4][FM_BLE_LINE_MAX]; };
static int receive(const char *s,size_t n,void *arg)
{
  struct received *r=arg;assert(r->count<4);
  memcpy(r->line[r->count],s,n+1);r->count++;return 0;
}
int main(void)
{
  struct fm_ble_frame f={0};struct received r={0};
  const char *s="{\"任务\":\"写报告\"}\r\nping\n";
  for(size_t i=0;i<strlen(s);i++)assert(!fm_ble_frame_feed(&f,s+i,1,receive,&r));
  assert(r.count==2 && !strcmp(r.line[0],"{\"任务\":\"写报告\"}") && !strcmp(r.line[1],"ping"));
  fm_ble_frame_reset(&f);r.count=0;
  char large[FM_BLE_LINE_MAX+40];memset(large,'x',sizeof(large));
  assert(fm_ble_frame_feed(&f,large,sizeof(large),receive,&r)==-EMSGSIZE);
  assert(!fm_ble_frame_feed(&f,"tail\npong\n",10,receive,&r));
  assert(r.count==1 && !strcmp(r.line[0],"pong"));
  fm_ble_frame_reset(&f);r.count=0;
  const char nul[]={'p',0,'x','\n','o','k','\n'};
  assert(fm_ble_frame_feed(&f,nul,sizeof(nul),receive,&r)==-EILSEQ);
  assert(r.count==1 && !strcmp(r.line[0],"ok"));
  fm_ble_frame_reset(&f);r.count=0;
  fm_ble_frame_feed(&f,"old",3,receive,&r);fm_ble_frame_reset(&f);
  fm_ble_frame_feed(&f,"new\n",4,receive,&r);
  assert(r.count==1 && !strcmp(r.line[0],"new"));
  puts("PASS: fragmented UTF-8, CRLF, oversized/NUL frame resync, reconnect reset");
  return 0;
}
