#include <assert.h>
#include <stdio.h>
#include "core/focus_flow.h"
int main(void)
{
  focus_flow f;
  flow_init(&f);flow_action(&f,"enter");flow_action(&f,"add");
  flow_action(&f,"talk");assert(f.page==P_RECORD);
  flow_capture_done(&f,true);
  assert(f.page==P_VOICE_READY && f.count==0 && !f.draft[0]);
  flow_action(&f,"confirm");assert(f.page==P_VOICE_READY && f.count==0);
  flow_action(&f,"talk");assert(f.page==P_RECORD);
  flow_capture_done(&f,false);assert(f.page==P_ADD && f.count==0);
  flow_action(&f,"talk");flow_action(&f,"home");
  flow_capture_done(&f,true);assert(f.page==P_STANDBY && f.count==0);
  flow_action(&f,"enter");flow_action(&f,"add");flow_action(&f,"talk");
  flow_capture_done(&f,true);flow_action(&f,"home");assert(f.page==P_STANDBY);
  flow_action(&f,"enter");flow_action(&f,"add");flow_action(&f,"talk");
  flow_action(&f,"cancel-record");assert(f.page==P_ADD);
  flow_capture_done(&f,true);assert(f.page==P_ADD && f.count==0);
  puts("PASS: raw recording is not a recognized task; retry, cancel and late completion respect navigation");
  return 0;
}
