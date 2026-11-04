/* Host holds cloud credentials and TLS. Device creates/parses LLM JSON.
 * A session-scoped USB transport, not a general IP network interface. */
#include "serial_transport.h"
#include "infra/vela_tls.h"
#include "infra/http_proxy.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t output_lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t ready=PTHREAD_COND_INITIALIZER;
static unsigned sequence, pending;
static int status;
static char *destination;
static size_t capacity;
static bool received;
void serial_emit(cJSON *root){
 char *s=cJSON_PrintUnformatted(root); if(!s)return;
 pthread_mutex_lock(&output_lock);
 printf("\n@FM@%s\n",s);fflush(stdout);
 pthread_mutex_unlock(&output_lock);free(s);
}
void serial_http_response(cJSON *root){
 cJSON *id=cJSON_GetObjectItem(root,"id"), *code=cJSON_GetObjectItem(root,"status"), *body=cJSON_GetObjectItem(root,"body");
 pthread_mutex_lock(&lock);
 if(cJSON_IsNumber(id)&&pending==(unsigned)id->valuedouble&&destination&&cJSON_IsNumber(code)&&cJSON_IsString(body)){
  size_t n=strlen(body->valuestring);
  status=n<capacity?code->valueint:VELA_TLS_ERR_OVERFLOW;
  if(n<capacity)memcpy(destination,body->valuestring,n+1);else destination[0]=0;
  received=true;pthread_cond_signal(&ready);
 }
 pthread_mutex_unlock(&lock);
}
static int relay(const char *host,const char *path,const char *body,char *resp,size_t cap){
 pthread_mutex_lock(&lock);
 if(pending){pthread_mutex_unlock(&lock);return VELA_TLS_ERR_CONNECT;}
 pending=++sequence;destination=resp;capacity=cap;received=false;resp[0]=0;
 cJSON *r=cJSON_CreateObject();cJSON_AddStringToObject(r,"op","http");cJSON_AddNumberToObject(r,"id",pending);
 cJSON_AddStringToObject(r,"host",host);cJSON_AddStringToObject(r,"path",path);cJSON_AddStringToObject(r,"body",body);cJSON_AddNumberToObject(r,"response_cap",cap);
 serial_emit(r);cJSON_Delete(r);
 struct timespec deadline;clock_gettime(CLOCK_REALTIME,&deadline);deadline.tv_sec+=45;
 while(!received){if(pthread_cond_timedwait(&ready,&lock,&deadline)!=0)break;}
 int result=received?status:VELA_TLS_ERR_READ;
 destination=NULL;capacity=0;pending=0;
 pthread_mutex_unlock(&lock);return result;
}
int vela_https_post_json(const char*h,const char*p,const char*path,const vela_header_t*headers,const char*body,char*resp,size_t cap){(void)p;(void)headers;return relay(h,path,body,resp,cap);}
int vela_http_post_json(const char*h,const char*p,const char*path,const vela_header_t*headers,const char*body,char*resp,size_t cap){(void)p;(void)headers;return relay(h,path,body,resp,cap);}
/* Unsupported proxy branch is explicitly disabled in this bring-up profile. */
bool http_proxy_is_enabled(void){return false;}
proxy_conn_t *proxy_conn_open(const char*h,int p,int t){(void)h;(void)p;(void)t;return NULL;}
int proxy_conn_write(proxy_conn_t*c,const char*d,int n){(void)c;(void)d;(void)n;return -1;}
int proxy_conn_read(proxy_conn_t*c,char*b,int n,int t){(void)c;(void)b;(void)n;(void)t;return -1;}
void proxy_conn_close(proxy_conn_t*c){(void)c;}
