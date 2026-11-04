/* SPDX-License-Identifier: Apache-2.0
 * Bounded raw HCI diagnostic. Run before the Bluetooth host owns ttyHCI0.
 * RESET and Read Local Version are forwarded to the real SiFli LCPU, unlike
 * several capability commands which this vendor adapter synthesizes.
 */
#include <nuttx/config.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <errno.h>
#include <time.h>
static uint64_t millis(void)
{
  struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);
  return (uint64_t)t.tv_sec*1000+t.tv_nsec/1000000;
}
static int command(int fd,uint16_t opcode)
{
  uint8_t cmd[]={1,opcode&255,opcode>>8,0};
  uint8_t event[258];size_t used=0,written=0;
  uint64_t deadline=millis()+8000;
  while(written<sizeof(cmd)) {
    ssize_t n=write(fd,cmd+written,sizeof(cmd)-written);
    if(n>0)written+=n;
    else if(n<0 && errno!=EAGAIN && errno!=EINTR)return -errno;
    if(millis()>deadline)return -ETIMEDOUT;
    if(n<=0)usleep(10000);
  }
  while(millis()<deadline) {
    struct pollfd p={.fd=fd,.events=POLLIN};
    if(poll(&p,1,100)<0 && errno!=EINTR)return -errno;
    uint8_t buf[128];ssize_t n=read(fd,buf,sizeof(buf));
    if(n<0) {if(errno==EAGAIN || errno==EINTR)continue;return -errno;}
    for(ssize_t i=0;i<n;i++) {
      uint8_t b=buf[i];
      if(!used && b!=4)continue;
      event[used++]=b;
      if(used>=3 && used==(size_t)event[2]+3) {
        if(event[1]==0x0e && used>=7 && event[4]==(opcode&255) && event[5]==(opcode>>8)) {
          printf("[ble-hci] opcode=%04x event=",opcode);
          for(size_t j=0;j<used;j++)printf("%02x",event[j]);
          printf(" status=%u\n",event[6]);fflush(stdout);
          return event[6] ? -EIO : 0;
        }
        if(event[1]==0x0f && used>=7 && event[5]==(opcode&255) && event[6]==(opcode>>8) && event[3])
          return -EIO;
        used=0;
      }
    }
  }
  return -ETIMEDOUT;
}
int focus_ble_hci_probe(void)
{
  int fd=open("/dev/ttyHCI0",O_RDWR|O_NONBLOCK);
  if(fd<0) {printf("[ble-hci] open failed errno=%d\n",errno);return -errno;}
  int ret=command(fd,0x0c03);
  if(!ret)ret=command(fd,0x1001);
  if(!ret)ret=command(fd,0x2002);
  close(fd);
  printf("[ble-hci] real-controller probe result=%d\n",ret);fflush(stdout);
  return ret;
}
