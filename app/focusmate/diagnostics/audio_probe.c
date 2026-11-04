/* SPDX-License-Identifier: Apache-2.0 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "focus_audio.h"
int main(int argc,char **argv)
{
  int ret = -1;
  if (argc == 3 && !strcmp(argv[1],"record")) {
    int seconds = atoi(argv[2]);
    if (seconds < 1 || seconds > 15) return 1;
    ret = focus_audio_begin();
    if (!ret) { sleep(seconds); ret = focus_audio_end(true); }
  } else if (argc == 2 && !strcmp(argv[1],"play")) {
    ret = focus_audio_play();
    if (!ret) {
      struct focus_audio_status status;
      do { usleep(10000); focus_audio_get(&status); } while (status.playing);
      ret = focus_audio_end(true);
    }
  } else if (argc == 2 && !strcmp(argv[1],"dump")) ret = focus_audio_dump();
  else if (argc == 2 && !strcmp(argv[1],"status")) { focus_audio_print(); ret = 0; }
  else puts("Usage: audio_probe record <1..15 seconds> | play | dump | status");
  printf("audio_probe: result=%d\n",ret);
  return ret < 0 ? 1 : 0;
}
