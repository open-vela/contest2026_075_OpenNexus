/* SPDX-License-Identifier: Apache-2.0 */
#include "ble_frame.h"
#include <errno.h>
#include <string.h>
void fm_ble_frame_reset(struct fm_ble_frame *p) { memset(p,0,sizeof(*p)); }
int fm_ble_frame_feed(struct fm_ble_frame *p,const void *data,size_t count,
                      fm_ble_line_cb emit,void *arg)
{
  const unsigned char *bytes=data;
  int result=0;
  for(size_t i=0;i<count;i++) {
    unsigned char c=bytes[i];
    if(c=='\n') {
      if(!p->dropping && p->used) {
        if(p->line[p->used-1]=='\r')p->used--;
        p->line[p->used]=0;
        if(p->used) {int ret=emit(p->line,p->used,arg);if(ret<0)result=ret;}
      }
      p->used=0;p->dropping=false;
    } else if(!p->dropping) {
      if(!c || p->used==sizeof(p->line)-1) {
        result=c ? -EMSGSIZE : -EILSEQ;p->used=0;p->dropping=true;
      } else p->line[p->used++]=(char)c;
    }
  }
  return result;
}
