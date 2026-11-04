/* Test the actual workspace cron source with a controlled clock. */
#include "agent_config.h"
#include "agent_compat.h"
#include <assert.h>
static char test_store[128];
static time_t clock_now=100000;
static time_t test_time(time_t *t){if(t)*t=clock_now;return clock_now;}
#undef AGENT_CRON_FILE
#define AGENT_CRON_FILE test_store
#define time test_time
#include "infra/cron_service.c"
#undef time
static int actions,notifications;
int tool_registry_execute(const char *name,const char *args,char *out,size_t cap){
 assert(!strcmp(name,"focusmate_prompt"));assert(!strcmp(args,"{\"title\":\"写报告\"}"));
 actions++;snprintf(out,cap,"UI event queued");return 0;
}
int message_bus_push_outbound(const agent_msg_t *msg){notifications++;free(msg->content);free(msg->image_b64);return 0;}
int main(void){
 snprintf(test_store,sizeof(test_store),"/tmp/focusmate-cron-test-%ld.json",(long)getpid());unlink(test_store);
 assert(cron_service_init()==0);
 cron_job_t j={0};j.kind=CRON_KIND_AT;j.at_epoch=clock_now+60;j.delete_after_run=true;
 strcpy(j.name,"test");strcpy(j.message,"写报告");strcpy(j.action,"focusmate_prompt");strcpy(j.action_args,"{\"title\":\"写报告\"}");
 assert(cron_add_job(&j)==0);assert(access(test_store,F_OK)==0);
 /* Verify load from persisted JSON before firing, not just in-memory state. */
 s_job_count=0;assert(cron_load_jobs()==0&&s_job_count==1);
 clock_now+=59;cron_process_due_jobs();assert(actions==0&&notifications==0);
 clock_now++;cron_process_due_jobs();assert(actions==1&&notifications==1&&s_job_count==0);
 cron_process_due_jobs();assert(actions==1);
 j.at_epoch=clock_now+60;assert(cron_add_job(&j)==0);assert(cron_remove_job(j.id)==0);
 clock_now+=61;cron_process_due_jobs();assert(actions==1&&s_job_count==0);
 unlink(test_store);puts("real cron source: save/reload/not-early/60-second-due/once-only/cancel PASS");
}
