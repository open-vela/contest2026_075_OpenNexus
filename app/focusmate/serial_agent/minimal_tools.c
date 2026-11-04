/* Explicit minimal tool surface for serial Agent bring-up. */
#include "tools/tool_registry.h"
#include "llm/llm_proxy.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int tool_registry_init(void){return 0;}
char *tool_registry_get_tools_json(void){return strdup("[]");}
void tool_registry_rebuild_json(void){}
void tool_registry_invalidate(void){}
void tool_registry_cleanup(void){}
int tool_registry_execute(const char*n,const char*i,char*out,size_t cap){(void)i;snprintf(out,cap,"Tool '%s' unavailable in serial validation profile",n);return -1;}
void tool_registry_register_provider(const char*n,tool_provider_fn p,tool_executor_fn e){(void)n;(void)p;(void)e;}
int node_manager_list(char *buf,size_t cap){if(cap)buf[0]=0;return 0;}
int llm_chat_vision(const char *prompt,const char *image,const char *type,char *out,size_t cap){(void)prompt;(void)image;(void)type;if(cap)out[0]=0;return -1;}
