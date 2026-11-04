/* SPDX-License-Identifier: Apache-2.0
 * Local test adapter: the UI thread owns all LVGL/state transitions. */
#include <mqueue.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
int main(int argc,char **argv)
{
  const char *allowed[] = {"status","enter","today","add","talk","home",
    "cancel-record","retry","play-recording","audio-status","audio-dump","reminder-start","reminder-dismiss",NULL};
  if (argc != 2) return 1;
  unsigned i; for (i = 0; allowed[i] && strcmp(allowed[i],argv[1]); ++i) { }
  if (!allowed[i]) { puts("focusmate_ctl: unsupported action"); return 1; }
  mqd_t mq = mq_open("/focusmate_ui",O_WRONLY|O_NONBLOCK);
  if (mq == (mqd_t)-1) { printf("focusmate_ctl: UI unavailable errno=%d\n",errno); return 1; }
  char action[32] = {0}; snprintf(action,sizeof(action),"%s",argv[1]);
  int ret = mq_send(mq,action,sizeof(action),0);
  mq_close(mq);
  return ret < 0 ? 1 : 0;
}
