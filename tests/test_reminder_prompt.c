/* SPDX-License-Identifier: Apache-2.0 */
#include "core/focus_flow.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
int fm_prompt_start(focus_flow *,const char *);
int main(void){
 focus_flow f;flow_init(&f);
 assert(fm_prompt_start(&f,"写报告")==0&&f.page==P_PREPARE&&f.count==1&&f.remaining==1500&&!f.round_open);
 flow_action(&f,"home");assert(fm_prompt_start(&f,"写报告")==0&&f.count==1);
 flow_action(&f,"start");assert(f.page==P_WAIT);
 flow_action(&f,"bypass");assert(f.page==P_RUNNING&&f.round_open);
 focus_flow before=f;assert(fm_prompt_start(&f,"另一任务")==-EBUSY&&!memcmp(&before,&f,sizeof(f)));
 flow_init(&f);f.count=FLOW_MAX_TASKS;for(int i=0;i<f.count;i++)snprintf(f.tasks[i].title,FLOW_TITLE,"task%d",i);
 before=f;assert(fm_prompt_start(&f,"新增任务")==-ENOSPC&&!memcmp(&before,&f,sizeof(f)));
 assert(fm_prompt_start(&f,"")==-EINVAL);
 puts("prompt state: create/deduplicate/prepare/wait/running-preservation/full-list/invalid-title PASS");
}
