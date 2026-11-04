/* SPDX-License-Identifier: Apache-2.0 */
#include "core/focus_flow.h"
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include "reminder.h"
#include "cJSON.h"
int fm_prompt_read(const struct fm_reminder_event *e,char *title,unsigned cap)
{
  if(e->version!=1||!memchr(e->key,0,sizeof(e->key))||strlen(e->key)!=16||strspn(e->key,"0123456789abcdef")!=16)return -EINVAL;
  char path[128],text[512];snprintf(path,sizeof(path),FM_REMINDER_EVENT_PATH,e->key);
  FILE *fp=fopen(path,"r");if(!fp)return -errno;
  size_t n=fread(text,1,sizeof(text)-1,fp);text[n]=0;fclose(fp);
  cJSON *j=cJSON_Parse(text),*v=j?cJSON_GetObjectItem(j,"title"):NULL;int r=-EINVAL;
  if(cJSON_IsString(v)&&v->valuestring[0]&&strlen(v->valuestring)<cap){snprintf(title,cap,"%s",v->valuestring);r=0;}
  cJSON_Delete(j);if(!r)unlink(path);return r;
}
int fm_prompt_start(focus_flow *f,const char *title)
{
  if(!title||!title[0]||strlen(title)>=FLOW_TITLE)return -EINVAL;
  if(f->round_open||f->review_pending||f->page==P_RECORD)return -EBUSY;
  int slot=-1;unsigned id=1;
  for(int i=0;i<f->count;i++){
    if(f->tasks[i].id>=id)id=f->tasks[i].id+1;
    if(!f->tasks[i].done&&!strcmp(f->tasks[i].title,title)){slot=i;break;}
  }
  if(slot<0){
    if(f->count>=FLOW_MAX_TASKS)return -ENOSPC;
    slot=f->count++;f->tasks[slot].id=id;f->tasks[slot].done=false;
    snprintf(f->tasks[slot].title,FLOW_TITLE,"%s",title);
  }
  f->cursor=slot;f->page=P_LIST;flow_action(f,"select");
  return f->page==P_PREPARE?0:-EINVAL;
}
