#include <nuttx/config.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "agent_compat.h"
#include "agent_config.h"
#include "core/agent_loop.h"
#include "core/message_bus.h"
#include "core/memory_store.h"
#include "core/session_mgr.h"
#include "infra/config_store.h"
#include "llm/llm_proxy.h"
#include "llm/llm_router.h"
#include "tools/skill_loader.h"
#include "tools/tool_guard.h"
#include "serial_transport.h"
bool agent_shutdown_requested(void){return false;}
void agent_request_shutdown(void){}
static void event(const char *op,const char *text){cJSON*r=cJSON_CreateObject();cJSON_AddStringToObject(r,"op",op);cJSON_AddStringToObject(r,"text",text);serial_emit(r);cJSON_Delete(r);}
static void command(char *line){
 cJSON*r=cJSON_Parse(line);if(!r){event("error","Invalid JSON frame");return;}
 cJSON*op=cJSON_GetObjectItem(r,"op");
 if(cJSON_IsString(op)&&!strcmp(op->valuestring,"http_response"))serial_http_response(r);
 else if(cJSON_IsString(op)&&!strcmp(op->valuestring,"ask")){
  cJSON*t=cJSON_GetObjectItem(r,"text");
  if(cJSON_IsString(t)&&strlen(t->valuestring)<=512){agent_msg_t msg={0};strcpy(msg.channel,"cli");strcpy(msg.chat_id,"usb-test");msg.content=strdup(t->valuestring);if(!msg.content||message_bus_push_inbound(&msg)!=0){free(msg.content);event("error","Queue failed");}else event("accepted","Device Agent received prompt");}
  else event("error","Prompt missing or too long");
 }
 else event("error","Unknown operation");
 cJSON_Delete(r);
}
int main(int argc,char **argv){
 (void)argc;(void)argv;
 static bool running;if(running){puts("serial_agent already running");return 1;}running=true;
 struct termios tty;if(tcgetattr(0,&tty)==0){tty.c_lflag&=~(ECHO|ICANON);tty.c_cc[VMIN]=1;tty.c_cc[VTIME]=0;tcsetattr(0,TCSANOW,&tty);}
 struct stat st;if(stat("/data",&st))mount(NULL,"/data","tmpfs",0,NULL);
 mkdir(AGENT_DATA_DIR,0755);mkdir(AGENT_CONFIG_DIR,0755);mkdir(AGENT_MEMORY_DIR,0755);mkdir(AGENT_SESSION_DIR,0755);mkdir(AGENT_SKILLS_DIR,0755);
 config_store_init();message_bus_init();memory_store_init();session_mgr_init();llm_proxy_init();
 /* Dummy token is non-secret: host injects real Authorization. */
 llm_set_all("api.deepseek.com","/v1/chat/completions","443","host-proxy","deepseek-chat");
 llm_router_init();tool_guard_init();skill_loader_init();agent_loop_init();
 char *line=malloc(24576);if(!line){event("error","No memory for frame reader");return 1;}
 if(agent_loop_start()!=0){event("error","Agent loop start failed");return 1;}
 event("ready","Upstream ai_agent core on SF32LB52; USB host HTTP transport; no tools/voice/IP interface");
 size_t used=0;bool overflow=false;
 for(;;){
  fd_set rd;FD_ZERO(&rd);FD_SET(0,&rd);struct timeval timeout={0,20000};
  if(select(1,&rd,NULL,NULL,&timeout)>0){char buf[128];ssize_t n=read(0,buf,sizeof(buf));
   for(ssize_t i=0;i<n;i++){char c=buf[i];if(c=='\n'){if(!overflow&&used){line[used]=0;command(line);}else if(overflow)event("error","Frame too long");used=0;overflow=false;}else if(c!='\r'){if(used<24575)line[used++]=c;else overflow=true;}}
  }
  agent_msg_t reply={0};if(message_bus_pop_outbound(&reply,0)==0){event("reply",reply.content?reply.content:"");message_bus_msg_free(&reply);}
 }
}
