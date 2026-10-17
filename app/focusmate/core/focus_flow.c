#include "focus_flow.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
static void copy(char *d, const char *s, size_t n) { snprintf(d,n,"%s",s); }
const char *flow_page_name(flow_page p) {
 static const char *names[]={"standby","welcome","list","add","recording","confirm-task","prepare","wait-phone","running","paused","end","review","review-more","help","summary","pause-menu","detail","note"};
 return p>=P_STANDBY && p<=P_NOTE?names[p]:"invalid";
}
const char *flow_title(const focus_flow *f) { return f->selected>=0 && f->selected<f->count?f->tasks[f->selected].title:"暂无行动"; }
static void list(focus_flow *f) {
 int i; f->page=P_LIST;
 if(f->cursor>=0 && f->cursor<f->count && !f->tasks[f->cursor].done) return;
 f->cursor=-1; for(i=0;i<f->count;i++) if(!f->tasks[i].done){ f->cursor=i; break; }
}
void flow_init(focus_flow *f) {
 memset(f,0,sizeof(*f)); f->page=P_STANDBY; f->selected=-1; f->cursor=-1;
 f->minutes=25; f->remaining=1500; f->sound=true; f->battery=-1;
 copy(f->speech,"嗯，我想，那个，写课程报告第一节",sizeof(f->speech));
}
static void request_start(focus_flow *f, bool resume) {
 f->wait_resume=resume; f->phone_pause=false;
 if(f->bypass || (f->phone_known && f->phone_stable && f->phone_raw)) {
   f->page=P_RUNNING; if(!f->round_open){ f->round_open=true; f->rounds++; }
 } else f->page=P_WAIT;
}
static void fresh(focus_flow *f) {
 f->remaining=f->minutes*60; f->round_seconds=0; f->round_open=false;
 f->bypass=false; f->phone_pause=false; f->review_pending=false; f->note[0]=0; f->page=P_PREPARE;
}
static void normalize(focus_flow *f) {
 const char *p=f->draft; bool changed=true;
 const char *fillers[]={"嗯","呃","那个","就是","我想要","我想","我要",NULL};
 while(changed){ int i; changed=false;
   for(i=0;fillers[i];i++) if(!strncmp(p,fillers[i],strlen(fillers[i]))){p+=strlen(fillers[i]);changed=true;break;}
   if(!strncmp(p,"，",3)){p+=3;changed=true;} else if(*p==',' || *p==' '){p++;changed=true;}
 }
 if(*p) memmove(f->draft,p,strlen(p)+1);
}
void flow_phone(focus_flow *f, bool present) {
 if(!f->phone_known || f->phone_raw!=present) { f->phone_raw=present; f->debounce_ms=650; }
}
void flow_poll(focus_flow *f, unsigned ms) {
 if(!f->debounce_ms)return;
 if(ms<f->debounce_ms){f->debounce_ms-=ms;return;}
 f->debounce_ms=0; f->phone_stable=f->phone_raw; f->phone_known=true;
 if(f->bypass)return;
 if(f->phone_stable && f->page==P_WAIT)request_start(f,f->wait_resume);
 else if(!f->phone_stable && f->page==P_RUNNING){f->page=P_PAUSED;f->phone_pause=true;f->interruptions++;}
 else if(f->phone_stable && f->page==P_PAUSED && f->phone_pause)request_start(f,true);
}
void flow_tick(focus_flow *f, unsigned secs) {
 if(f->page!=P_RUNNING)return;
 if(secs>(unsigned)f->remaining)secs=f->remaining;
 f->remaining-=secs; f->round_seconds+=secs; f->total_seconds+=secs;
 if(!f->remaining){f->page=P_REVIEW;f->round_open=false;f->phone_pause=false;f->review_pending=true;}
}
void flow_action(focus_flow *f, const char *a) {
 f->notice[0]=0;
 if(!strcmp(a,"home")) {
   if(f->page==P_RUNNING)f->interruptions++;
   if(f->page==P_RECORD)f->draft[0]=0;
   f->phone_pause=false; f->page=P_STANDBY;
 } else if(!strcmp(a,"enter")||!strcmp(a,"wake"))f->page=P_WELCOME;
 else if(!strcmp(a,"today")) {if(f->review_pending)f->page=P_REVIEW;else if(f->round_open)f->page=P_PAUSED; else list(f);}
 else if(!strcmp(a,"welcome"))f->page=P_WELCOME;
 else if(!strcmp(a,"list"))list(f);
 else if(!strcmp(a,"add"))f->page=P_ADD;
 else if(!strcmp(a,"talk")) {
   if(f->page==P_RECORD) {
     if(!f->heard || !f->draft[0]) {f->page=P_ADD;copy(f->notice,"未收到声音，请重试",sizeof(f->notice));}
     else {normalize(f);f->page=P_CONFIRM;}
   } else if(f->page!=P_CONFIRM && f->page!=P_REVIEW && f->page!=P_END) {
     if(f->page==P_RUNNING) {f->interruptions++;f->phone_pause=false;}
     f->before_record=f->round_open?P_PAUSED:P_ADD;f->page=P_RECORD;f->heard=false;f->draft[0]=0;
   }
 } else if(!strcmp(a,"cancel-record")){f->draft[0]=0;f->page=f->before_record;}
 else if(!strcmp(a,"retry")){f->draft[0]=0;f->page=P_ADD;}
 else if(!strcmp(a,"confirm") && f->page==P_CONFIRM) {
   int i,slot=-1; unsigned id=1;
   for(i=0;i<f->count;i++){if(f->tasks[i].id>=id)id=f->tasks[i].id+1;if(f->tasks[i].done && slot<0)slot=i;}
   if(f->count<FLOW_MAX_TASKS)slot=f->count++;
   if(slot<0){copy(f->notice,"任务已满，请先完成一项",sizeof(f->notice));return;}
   f->tasks[slot].id=id;f->tasks[slot].done=false;copy(f->tasks[slot].title,f->draft,FLOW_TITLE);
   f->draft[0]=0;f->page=P_ADD;copy(f->notice,"已添加，可继续添加行动",sizeof(f->notice));
 } else if(!strcmp(a,"prev")||!strcmp(a,"next")) {
   int i,dir=!strcmp(a,"next")?1:-1;for(i=f->cursor+dir;i>=0 && i<f->count;i+=dir)if(!f->tasks[i].done){f->cursor=i;break;}
 } else if(!strcmp(a,"select") && f->page==P_LIST && f->cursor>=0 && f->cursor<f->count && !f->tasks[f->cursor].done) {
   if(f->round_open){copy(f->notice,"请先结束当前专注轮次",sizeof(f->notice));return;}
   f->selected=f->cursor;fresh(f);
 } else if(!strncmp(a,"minutes ",8) && f->page==P_PREPARE) {
   int m=atoi(a+8);if(m==15||m==25||m==45){f->minutes=m;f->remaining=m*60;}
 } else if(!strcmp(a,"start") && f->page==P_PREPARE && f->selected>=0)request_start(f,false);
 else if(!strcmp(a,"cancel-wait") && f->page==P_WAIT){f->page=f->wait_resume?P_PAUSED:P_PREPARE;f->phone_pause=false;}
 else if(!strcmp(a,"bypass") && f->page==P_WAIT){f->bypass=true;request_start(f,f->wait_resume);}
 else if(!strcmp(a,"pause") && f->page==P_RUNNING){f->page=P_PAUSED;f->phone_pause=false;f->interruptions++;}
 else if(!strcmp(a,"resume") && f->page==P_PAUSED)request_start(f,true);
 else if(!strcmp(a,"pause-menu"))f->page=P_PAUSE_MENU;
 else if(!strcmp(a,"paused"))f->page=P_PAUSED;
 else if(!strcmp(a,"detail"))f->page=P_DETAIL;
 else if(!strcmp(a,"note"))f->page=P_NOTE;
 else if(!strcmp(a,"save-note"))f->page=P_PAUSED;
 else if(!strcmp(a,"end") && f->round_open){f->before_end=f->page==P_RUNNING?P_RUNNING:P_PAUSED;f->page=P_END;f->phone_pause=false;}
 else if(!strcmp(a,"cancel-end") && f->page==P_END){if(f->before_end==P_RUNNING)request_start(f,true);else f->page=P_PAUSED;}
 else if(!strcmp(a,"confirm-end") && f->page==P_END){f->page=P_REVIEW;f->round_open=false;f->review_pending=true;}
 else if(!strcmp(a,"timeout") && f->page==P_RUNNING)flow_tick(f,f->remaining);
 else if(!strcmp(a,"done") && f->page==P_REVIEW){if(f->selected>=0)f->tasks[f->selected].done=true;f->review_pending=false;f->page=P_SUMMARY;}
 else if(!strcmp(a,"continue") && (f->page==P_REVIEW||f->page==P_MORE))fresh(f);
 else if(!strcmp(a,"more") && f->page==P_REVIEW)f->page=P_MORE;
 else if(!strcmp(a,"review"))f->page=P_REVIEW;
 else if(!strcmp(a,"help") && f->page==P_MORE)f->page=P_HELP;
 else if(!strcmp(a,"accept-help") && f->page==P_HELP){
   if(f->selected>=0){copy(f->tasks[f->selected].title,"为当前任务列出三个要点",FLOW_TITLE);}
   fresh(f);
 } else if(!strcmp(a,"summary") && f->page==P_MORE){f->review_pending=false;f->page=P_SUMMARY;}
}
