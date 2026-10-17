/* Pixel prototype migration. Single owner for LVGL, input and flow state.
 * Lab controls are simulator adapters, not hardware capability claims. */
#include <nuttx/config.h>
#include <lvgl/lvgl.h>
#include <netutils/cJSON.h>
#include <nuttx/input/buttons.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "core/focus_flow.h"
#ifdef CONFIG_EXAMPLES_AI_AGENT_VELA
#include "voice/voice_channel.h"
#endif
extern const lv_font_t lv_font_simsun_24_cjk;
#ifndef CONFIG_FOCUSMATE_HOME_MASK
#define CONFIG_FOCUSMATE_HOME_MASK 0
#endif
#ifndef CONFIG_FOCUSMATE_TALK_MASK
#define CONFIG_FOCUSMATE_TALK_MASK 0
#endif
#define INK 0xe1edc8
#define BG 0x14251e
#ifndef CONFIG_FOCUSMATE_DATA_DIR
#define CONFIG_FOCUSMATE_DATA_DIR "/data/focusmate"
#endif
#define SNAP CONFIG_FOCUSMATE_DATA_DIR "/pixel-v1.json"
static focus_flow f;
static lv_nuttx_result_t display_resources;
static lv_obj_t *panel,*body,*status,*timer_label,*wave[23],*notice_label,*note_input;
static int pending, lab_sample, real_voice, voice_active, buttons_fd=-1;
static char pending_action[40];
static const char *samples[]={"嗯,我想,那个,写课程报告第一节","修改简历中的项目经历","呃,我要,练习两道面试题"};
static const char *petrows[]={"..pp....pp..","..pppppppp..",".pppppppppp.","pppppppppppp","pppeppppeppp","pppeppppeppp","ppppccccpppp",".pppppppppp.","..pppppppp..","..pp....pp.."};
static void textcopy(char *d,const char *v,size_t cap){size_t n=strlen(v);if(n>=cap)n=cap-1;while(n && ((unsigned char)v[n]&0xc0)==0x80)n--;memcpy(d,v,n);d[n]=0;}
static unsigned long long now_ms(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (unsigned long long)t.tv_sec*1000+t.tv_nsec/1000000;}
static void label_color(lv_obj_t *o,unsigned c){lv_obj_set_style_text_color(o,lv_color_hex(c),0);}
static lv_obj_t *label(lv_obj_t *parent,const char *text){
 lv_obj_t *o=lv_label_create(parent);lv_obj_set_width(o,LV_PCT(100));lv_label_set_long_mode(o,LV_LABEL_LONG_WRAP);
 label_color(o,INK);lv_label_set_text(o,text);lv_obj_set_style_text_align(o,LV_TEXT_ALIGN_CENTER,0);return o;
}
static void queue(lv_event_t *e){if(!pending){snprintf(pending_action,sizeof(pending_action),"%s",(const char *)lv_event_get_user_data(e));pending=1;}}
static lv_obj_t *button(lv_obj_t *parent,const char *text,const char *a,bool secondary){
 lv_obj_t *b=lv_button_create(parent);lv_obj_set_size(b,LV_PCT(100),48);lv_obj_set_style_radius(b,3,0);
 lv_obj_set_style_bg_color(b,lv_color_hex(secondary?0x344b35:0xd5e6ad),0);
 lv_obj_set_style_text_color(b,lv_color_hex(secondary?INK:BG),0);lv_obj_set_style_shadow_width(b,0,0);
 lv_obj_add_event_cb(b,queue,LV_EVENT_CLICKED,(void *)a);lv_obj_t *l=lv_label_create(b);lv_label_set_text(l,text);lv_obj_center(l);return b;
}
static void spacer(void){lv_obj_t *s=lv_obj_create(body);lv_obj_remove_style_all(s);lv_obj_set_size(s,1,1);lv_obj_set_flex_grow(s,1);}
static void cat(int unit){
 int x,y;lv_obj_t *c=lv_obj_create(body);lv_obj_remove_style_all(c);lv_obj_set_size(c,12*unit,10*unit);
 lv_obj_remove_flag(c,LV_OBJ_FLAG_SCROLLABLE);
 for(y=0;y<10;y++)for(x=0;x<12;x++)if(petrows[y][x]!='.'){
   lv_obj_t *p=lv_obj_create(c);lv_obj_remove_style_all(p);lv_obj_set_pos(p,x*unit,y*unit);lv_obj_set_size(p,unit,unit);
   lv_obj_set_style_bg_opa(p,LV_OPA_COVER,0);lv_obj_set_style_bg_color(p,lv_color_hex(petrows[y][x]=='e'?BG:petrows[y][x]=='c'?0x74934e:0xbfdb8b),0);
 }
}
static void heading(const char *t){label(body,t);}
static void back(const char *a){lv_obj_t *b=button(body,"< 返回",a,true);lv_obj_set_height(b,36);}
static void save(void){
 cJSON *j=cJSON_CreateObject(),*ts=cJSON_CreateArray();int i;
 if(!j||!ts){cJSON_Delete(j);cJSON_Delete(ts);return;}
 cJSON_AddNumberToObject(j,"schema",1);
 cJSON_AddNumberToObject(j,"selected",f.selected);cJSON_AddNumberToObject(j,"minutes",f.minutes);
 cJSON_AddNumberToObject(j,"remaining",f.remaining);cJSON_AddNumberToObject(j,"round_seconds",f.round_seconds);
 cJSON_AddNumberToObject(j,"total_seconds",f.total_seconds);cJSON_AddNumberToObject(j,"rounds",f.rounds);
 cJSON_AddNumberToObject(j,"interruptions",f.interruptions);cJSON_AddBoolToObject(j,"round_open",f.round_open);cJSON_AddBoolToObject(j,"review_pending",f.review_pending);
 cJSON_AddStringToObject(j,"note",f.note);cJSON_AddItemToObject(j,"tasks",ts);
 for(i=0;i<f.count;i++){cJSON *t=cJSON_CreateObject();cJSON_AddNumberToObject(t,"id",f.tasks[i].id);cJSON_AddStringToObject(t,"title",f.tasks[i].title);cJSON_AddBoolToObject(t,"done",f.tasks[i].done);cJSON_AddItemToArray(ts,t);}
 char *s=cJSON_PrintUnformatted(j);if(s){FILE *fp=fopen(SNAP ".tmp","w");if(fp){bool ok=fwrite(s,1,strlen(s),fp)==strlen(s);if(fclose(fp)!=0)ok=false;if(ok)rename(SNAP ".tmp",SNAP);else snprintf(f.notice,sizeof(f.notice),"记录保存失败");}else snprintf(f.notice,sizeof(f.notice),"记录保存失败");cJSON_free(s);}cJSON_Delete(j);
}
static int number(cJSON *j,const char *k,int fallback){cJSON *v=cJSON_GetObjectItem(j,k);return cJSON_IsNumber(v)?v->valueint:fallback;}
static void load(void){
 char buf[8192];FILE *fp=fopen(SNAP,"r");if(!fp)return;size_t n=fread(buf,1,sizeof(buf)-1,fp);fclose(fp);buf[n]=0;
 cJSON *j=cJSON_Parse(buf);if(!j)return;if(number(j,"schema",0)!=1){cJSON_Delete(j);return;}
 cJSON *ts=cJSON_GetObjectItem(j,"tasks");int i;
 for(i=0;cJSON_IsArray(ts)&&i<cJSON_GetArraySize(ts)&&i<FLOW_MAX_TASKS;i++){
   cJSON *t=cJSON_GetArrayItem(ts,i),*title=cJSON_GetObjectItem(t,"title");if(!cJSON_IsString(title)||!title->valuestring[0])continue;
   flow_task *d=&f.tasks[f.count++];d->id=number(t,"id",i+1);d->done=cJSON_IsTrue(cJSON_GetObjectItem(t,"done"));snprintf(d->title,sizeof(d->title),"%s",title->valuestring);
 }
 f.selected=number(j,"selected",-1);if(f.selected<0||f.selected>=f.count)f.selected=-1;
 f.minutes=number(j,"minutes",25);if(f.minutes!=15&&f.minutes!=25&&f.minutes!=45)f.minutes=25;
 f.remaining=number(j,"remaining",f.minutes*60);if(f.remaining<0||f.remaining>f.minutes*60)f.remaining=f.minutes*60;
 f.round_seconds=number(j,"round_seconds",0);f.total_seconds=number(j,"total_seconds",0);f.rounds=number(j,"rounds",0);f.interruptions=number(j,"interruptions",0);
 f.round_open=cJSON_IsTrue(cJSON_GetObjectItem(j,"round_open"))&&f.selected>=0&&f.remaining>0;f.recovered=f.round_open;f.review_pending=cJSON_IsTrue(cJSON_GetObjectItem(j,"review_pending"))&&f.selected>=0;
 cJSON *note=cJSON_GetObjectItem(j,"note");if(cJSON_IsString(note))snprintf(f.note,sizeof(f.note),"%s",note->valuestring);
 cJSON_Delete(j);
}
static void status_refresh(void){char s[100];
 if(f.battery_known)snprintf(s,sizeof(s),"FOCUSMATE  %s  %d%%%s",f.network?"SIM NET":"OFFLINE",f.battery,f.charging?" +":"");
 else snprintf(s,sizeof(s),"FOCUSMATE  %s  USB / BAT ?",f.network?"SIM NET":"OFFLINE");
 lv_label_set_text(status,s);
}
static void render(void){
 char b[256];int i;lv_obj_clean(body);timer_label=NULL;memset(wave,0,sizeof(wave));notice_label=NULL;note_input=NULL;status_refresh();
 switch(f.page){
 case P_STANDBY:spacer();cat(10);heading("FocusMate");heading("说 你好,openvela 唤醒");spacer();button(body,"点击进入","enter",false);break;
 case P_WELCOME:cat(7);heading("你好.");heading("今天,想从哪一步开始?");spacer();button(body,"今日任务","today",false);button(body,"添加行动","add",true);break;
 case P_LIST:
   back("welcome");cat(4);heading("选择今日行动");
   if(f.cursor>=0 && f.cursor<f.count)heading(f.tasks[f.cursor].title);else heading("暂无待办,先添加行动");spacer();
   {lv_obj_t *row=lv_obj_create(body);lv_obj_remove_style_all(row);lv_obj_set_size(row,LV_PCT(100),45);lv_obj_set_flex_flow(row,LV_FLEX_FLOW_ROW);lv_obj_set_style_pad_column(row,8,0);
    lv_obj_t *p=button(row,"上一项","prev",true);lv_obj_set_width(p,LV_PCT(48));p=button(row,"下一项","next",true);lv_obj_set_width(p,LV_PCT(48));}
   if(f.cursor>=0)button(body,"选择这项","select",false);else button(body,"添加行动","add",false);break;
 case P_ADD:back("welcome");cat(7);heading("这次想做什么?");spacer();heading("按一下对话键,开始说话\n再按一下,结束识别");spacer();break;
 case P_RECORD:cat(3);heading("正在听你说...");
   {lv_obj_t *row=lv_obj_create(body);lv_obj_remove_style_all(row);lv_obj_set_size(row,LV_PCT(100),72);lv_obj_set_flex_flow(row,LV_FLEX_FLOW_ROW);lv_obj_set_flex_align(row,LV_FLEX_ALIGN_CENTER,LV_FLEX_ALIGN_CENTER,LV_FLEX_ALIGN_CENTER);lv_obj_set_style_pad_column(row,5,0);
    for(i=0;i<23;i++){wave[i]=lv_obj_create(row);lv_obj_remove_style_all(wave[i]);lv_obj_set_size(wave[i],6,4);lv_obj_set_style_bg_opa(wave[i],LV_OPA_COVER,0);lv_obj_set_style_bg_color(wave[i],lv_color_hex(0xbfdb8b),0);}}
   heading(real_voice?"真实收音模式":"模拟收音模式");spacer();heading("再按一下对话键\n结束语音识别");button(body,"取消","cancel-record",true);break;
 case P_CONFIRM:back("retry");cat(4);heading("任务名对吗?");heading(f.draft);spacer();button(body,"确认无误","confirm",false);button(body,"重新输入","retry",true);break;
 case P_PREPARE:back("list");cat(4);heading(flow_title(&f));snprintf(b,sizeof(b),"本轮 %d 分钟",f.minutes);heading(b);spacer();
   {lv_obj_t *row=lv_obj_create(body);lv_obj_remove_style_all(row);lv_obj_set_size(row,LV_PCT(100),48);lv_obj_set_flex_flow(row,LV_FLEX_FLOW_ROW);lv_obj_set_style_pad_column(row,5,0);
    const char *txt[]={"15分","25分","45分"},*act[]={"minutes 15","minutes 25","minutes 45"};for(i=0;i<3;i++){lv_obj_t *p=button(row,txt[i],act[i],f.minutes!=(i==0?15:i==1?25:45));lv_obj_set_width(p,LV_PCT(32));}}
   button(body,"开始","start",false);break;
 case P_WAIT:heading(f.wait_resume?"放下手机,继续专注":"放下手机,开始专注");cat(7);heading("v\n将手机放入底座\n计时会自动开始");spacer();button(body,"取消","cancel-wait",true);button(body,"无法检测?直接开始","bypass",true);break;
 case P_RUNNING:case P_PAUSED:
   cat(3);heading(flow_title(&f));snprintf(b,sizeof(b),"%02d:%02d",f.remaining/60,f.remaining%60);timer_label=label(body,b);
   lv_obj_set_style_text_font(timer_label,&lv_font_montserrat_40,0);
   heading(f.page==P_RUNNING?(f.bypass?"专注中 / 独立计时":"专注中 / 手机已停靠"):(f.phone_pause?"手机已拿起 / 计时暂停":"已暂停"));spacer();
   if(f.page==P_RUNNING){button(body,"暂停","pause",false);button(body,"结束本轮","end",true);}
   else {if(f.phone_pause)heading("放回手机,自动继续");else button(body,"继续","resume",false);button(body,"更多","pause-menu",true);}break;
 case P_END:cat(4);heading("结束本轮?");snprintf(b,sizeof(b),"已投入 %d 分 %d 秒\n记录会保留",f.round_seconds/60,f.round_seconds%60);heading(b);spacer();button(body,"返回","cancel-end",false);button(body,"确认结束","confirm-end",true);break;
 case P_REVIEW:cat(5);heading("这一轮结束了");snprintf(b,sizeof(b),"专注 %d 分 %d 秒",f.round_seconds/60,f.round_seconds%60);heading(b);spacer();button(body,"完成了","done",false);button(body,"继续做","continue",true);button(body,"其他","more",true);break;
 case P_MORE:back("review");spacer();button(body,"遇到困难","help",true);button(body,"今天先结束","summary",true);spacer();break;
 case P_HELP:back("review");cat(4);heading("缩小一步 / 本地模拟建议");heading("先为当前任务列出三个要点");spacer();button(body,"采用建议","accept-help",false);button(body,"保留原行动","review",true);break;
 case P_SUMMARY:cat(7);heading("这段投入,已保存.");snprintf(b,sizeof(b),"%d 分钟\n结束 %d 轮专注",f.total_seconds/60,f.rounds);heading(b);spacer();button(body,"返回任务列表","list",false);break;
 case P_PAUSE_MENU:back("paused");spacer();button(body,"恢复线索","detail",true);button(body,"留下备注","note",true);button(body,"结束本轮","end",true);break;
 case P_DETAIL:back("paused");heading(flow_title(&f));heading(f.note[0]?f.note:"先推进一小步,一轮结束后再决定.");break;
 case P_NOTE:back("paused");heading("下次从哪里继续?");note_input=lv_textarea_create(body);lv_obj_set_size(note_input,LV_PCT(100),120);lv_textarea_set_max_length(note_input,50);lv_textarea_set_text(note_input,f.note);spacer();button(body,"保存","save-note",false);break;
 }
 if(f.notice[0]){notice_label=label(body,f.notice);label_color(notice_label,0xf2bb9f);}
 printf("[pixel] page=%s tasks=%d remaining=%d phone=%d/%d round=%d\n",flow_page_name(f.page),f.count,f.remaining,f.phone_known,f.phone_stable,f.round_open);fflush(stdout);
}
static void voice_stop_cancel(void){
#ifdef CONFIG_EXAMPLES_AI_AGENT_VELA
 if(voice_active){char discard[8];voice_channel_stop_with_text(discard,sizeof(discard));voice_active=0;}
#endif
}
static void dispatch(const char *a){
 if(!strcmp(a,"phone-in")){flow_phone(&f,true);return;}
 if(!strcmp(a,"phone-out")){flow_phone(&f,false);return;}
 if(!strcmp(a,"sound")){f.sound=!f.sound;return;}
 if(!strcmp(a,"sample")){lab_sample=(lab_sample+1)%3;snprintf(f.speech,sizeof(f.speech),"%s",samples[lab_sample]);snprintf(f.notice,sizeof(f.notice),"样例:%s",f.speech);render();return;}
 if(!strcmp(a,"network")){f.network=!f.network;snprintf(f.notice,sizeof(f.notice),f.network?"模拟联网已恢复":"模拟断网:任务和计时可离线使用");render();return;}
 if(!strcmp(a,"battery")){f.battery_known=true;f.battery=15;f.charging=false;snprintf(f.notice,sizeof(f.notice),"模拟低电量,请连接电源");render();return;}
 if(!strcmp(a,"charging")){f.battery_known=true;f.battery=60;f.charging=true;render();return;}
 if(!strcmp(a,"usb")){f.battery_known=false;f.charging=false;render();return;}
 if(!strcmp(a,"fast")){flow_tick(&f,300);save();render();return;}
 if(!strcmp(a,"voice-mode")){if(f.page!=P_RECORD){real_voice=!real_voice;snprintf(f.notice,sizeof(f.notice),real_voice?"真实音频需驱动与 ASR 配置":"模拟收音:不调用真实麦克风");render();}return;}
 if(!strcmp(a,"talk") && real_voice){
#ifdef CONFIG_EXAMPLES_AI_AGENT_VELA
   if(f.page==P_RECORD && voice_active){int ret=voice_channel_stop_with_text(f.draft,sizeof(f.draft));voice_active=0;f.heard=ret==0&&f.draft[0];}
   else if(f.page!=P_CONFIRM&&f.page!=P_REVIEW&&f.page!=P_END){int ret=voice_channel_start();if(ret){snprintf(f.notice,sizeof(f.notice),"录音启动失败:%d",ret);render();return;}voice_active=1;}
#else
   snprintf(f.notice,sizeof(f.notice),"此固件未编入语音接口");render();return;
#endif
 }
 if((!strcmp(a,"home")||!strcmp(a,"cancel-record"))&&voice_active)voice_stop_cancel();
 if(!strcmp(a,"save-note")&&note_input)textcopy(f.note,lv_textarea_get_text(note_input),sizeof(f.note));
 flow_action(&f,a);save();render();
}
static void lab(lv_obj_t *root){
 lv_obj_t *box=lv_obj_create(root);lv_obj_set_pos(box,425,10);lv_obj_set_size(box,425,570);lv_obj_set_flex_flow(box,LV_FLEX_FLOW_COLUMN);lv_obj_set_style_pad_row(box,5,0);lv_obj_set_style_bg_color(box,lv_color_hex(0x263e2b),0);
 label(box,"模拟器测试台 / 非设备界面");
 const char *txt[]={"HOME:回待机","对话:按一下开始 / 再按结束","模拟唤醒","放入手机","拿起手机","模拟声音 开 / 关","切换语音样例","快进 5 分钟","模拟本轮到时","网络 在线 / 离线","模拟低电量","模拟充电","USB / 电量未知","切换真实 / 模拟收音"};
 const char *act[]={"home","talk","wake","phone-in","phone-out","sound","sample","fast","timeout","network","battery","charging","usb","voice-mode"};int i;
 for(i=0;i<14;i++){lv_obj_t *b=button(box,txt[i],act[i],true);lv_obj_set_height(b,42);}
}
static int display_init(void){
 lv_nuttx_dsc_t d;lv_nuttx_result_t r;memset(&r,0,sizeof(r));if(!lv_is_initialized())lv_init();
 if(!lv_display_get_default()){lv_nuttx_dsc_init(&d);d.fb_path=access("/dev/lcd0",F_OK)==0?"/dev/lcd0":"/dev/fb0";d.input_path="/dev/input0";lv_nuttx_init(&d,&r);if(!r.disp)return -1;display_resources=r;}
 lv_obj_t *root=lv_screen_active();lv_obj_clean(root);lv_obj_set_style_bg_color(root,lv_color_hex(0x0c1812),0);lv_obj_set_style_text_font(root,&lv_font_simsun_24_cjk,0);label_color(root,INK);
 int w=lv_display_get_horizontal_resolution(NULL),h=lv_display_get_vertical_resolution(NULL);
 panel=lv_obj_create(root);lv_obj_remove_style_all(panel);lv_obj_set_size(panel,w>=720?390:w,h>=450?450:h);lv_obj_set_pos(panel,w>=720?15:0,w>=720?15:0);lv_obj_set_style_bg_color(panel,lv_color_hex(BG),0);lv_obj_set_style_bg_opa(panel,LV_OPA_COVER,0);lv_obj_remove_flag(panel,LV_OBJ_FLAG_SCROLLABLE);
 status=label(panel,"");lv_obj_set_pos(status,0,8);lv_obj_set_height(status,30);lv_obj_set_style_text_font(status,&lv_font_montserrat_14,0);
 body=lv_obj_create(panel);lv_obj_remove_style_all(body);lv_obj_set_pos(body,0,38);lv_obj_set_size(body,LV_PCT(100),(h>=450?450:h)-38);lv_obj_set_style_pad_all(body,14,0);lv_obj_set_style_pad_row(body,6,0);lv_obj_set_flex_flow(body,LV_FLEX_FLOW_COLUMN);lv_obj_set_flex_align(body,LV_FLEX_ALIGN_START,LV_FLEX_ALIGN_CENTER,LV_FLEX_ALIGN_CENTER);
 if(w>=720)lab(root);
 return 0;
}
static void cli(char *line){
 line[strcspn(line,"\r\n")]=0;
 if(!strncmp(line,"speech ",7)){textcopy(f.speech,line+7,sizeof(f.speech));return;}
 if(!strncmp(line,"note ",5)){textcopy(f.note,line+5,sizeof(f.note));save();render();return;}
 if(!strcmp(line,"status")){printf("[pixel-status] page=%s tasks=%d selected=%d remaining=%d seconds=%d total=%d phone=%d/%d pause=%s voice=%s\n",flow_page_name(f.page),f.count,f.selected,f.remaining,f.round_seconds,f.total_seconds,f.phone_known,f.phone_stable,f.phone_pause?"phone":"manual",real_voice?"real":"mock");return;}
 if(!strncmp(line,"asr ",4)||!strncmp(line,"speak ",6)){
#ifdef CONFIG_EXAMPLES_AI_AGENT_VELA
   voice_channel_init();
   int ret=!strncmp(line,"asr ",4)?voice_channel_test_asr(line+4):voice_channel_speak(line+6);
   printf("[audio] request result=%d; ASR test is asynchronous; inspect backend logs\n",ret);
#else
   printf("[audio] backend not compiled\n");
#endif
   return;
 }
 if(!strncmp(line,"pcm ",4)){
   FILE *fp=fopen(line+4,"rb");if(!fp){printf("[audio] cannot open PCM file\n");return;}short v;unsigned long n=0;int peak=0;while(fread(&v,sizeof(v),1,fp)==1){int amp=v<0?-(int)v:v;if(amp>peak)peak=amp;n++;}fclose(fp);printf("[audio] PCM s16le mono 16000Hz contract: samples=%lu peak=%d; probe only, no ASR/playback\n",n,peak);return;
 }
 dispatch(line);
}
int focus_pixel_main(int argc,char **argv){
 (void)argc;(void)argv;pending=0;real_voice=0;voice_active=0;lab_sample=0;flow_init(&f);mkdir(CONFIG_FOCUSMATE_DATA_DIR,0755);bool fresh_data=access(SNAP,F_OK)!=0;load();
 if(fresh_data){const char *t[]={"整理简历中的项目经历","列出课程报告的目录","练习两道面试题"};int i;for(i=0;i<3;i++){f.tasks[i].id=i+1;textcopy(f.tasks[i].title,t[i],FLOW_TITLE);}f.count=3;}
 if(display_init())return 1;
 buttons_fd=open("/dev/buttons",O_RDONLY|O_NONBLOCK);btn_buttonset_t oldkeys=0;
 printf("FocusMate pixel UI. CLI: enter today add talk confirm retry list select minutes 15|25|45 start phone-in phone-out pause resume home fast timeout done status; speech <text>; pcm <path>. Legacy: focusmate --legacy\n");
 render();unsigned long long prev=now_ms(),tick_acc=0,wave_acc=0,save_acc=0;unsigned phase=0;char line[320];size_t used=0;bool quit=false;
 while(!quit){
   unsigned long long current=now_ms();unsigned dt=(unsigned)(current-prev);prev=current;
   flow_page old=f.page;bool known=f.phone_known,stable=f.phone_stable;flow_poll(&f,dt);
   if(old!=f.page||known!=f.phone_known||stable!=f.phone_stable){save();render();}
   if(f.page==P_RUNNING){tick_acc+=dt;if(tick_acc>=1000){unsigned sec=tick_acc/1000;tick_acc%=1000;flow_tick(&f,sec);if(f.page!=P_RUNNING){save();render();}else if(timer_label){char t[20];snprintf(t,sizeof(t),"%02d:%02d",f.remaining/60,f.remaining%60);lv_label_set_text(timer_label,t);}}}else tick_acc=0;
   save_acc+=dt;if(save_acc>=10000){save_acc=0;if(f.page==P_RUNNING)save();}
   if(f.page==P_RECORD&&!real_voice){wave_acc+=dt;if(wave_acc>=160){wave_acc=0;phase++;if(f.sound){f.heard=true;snprintf(f.draft,sizeof(f.draft),"%s",f.speech);}int i;for(i=0;i<23;i++)if(wave[i])lv_obj_set_height(wave[i],f.sound?8+(i*17+phase*23)%56:4);}}
   if(buttons_fd>=0){btn_buttonset_t keys=0;if(read(buttons_fd,&keys,sizeof(keys))==sizeof(keys)){btn_buttonset_t pressed=keys&~oldkeys;if(pressed&CONFIG_FOCUSMATE_HOME_MASK)dispatch("home");if(pressed&CONFIG_FOCUSMATE_TALK_MASK)dispatch("talk");oldkeys=keys;}}
   lv_timer_handler();if(pending){char a[40];snprintf(a,sizeof(a),"%s",pending_action);pending=0;dispatch(a);}
   fd_set rd;FD_ZERO(&rd);FD_SET(0,&rd);struct timeval timeout={0,20000};int ready=select(1,&rd,NULL,NULL,&timeout);
   if(ready>0){char c;if(read(0,&c,1)==1){if(c=='\n'||c=='\r'){line[used]=0;if(!strcmp(line,"quit"))quit=true;else if(used)cli(line);used=0;}else if(used<sizeof(line)-1)line[used++]=c;}}
 }
 voice_stop_cancel();save();if(buttons_fd>=0)close(buttons_fd);lv_nuttx_deinit(&display_resources);return 0;
}
