/* SPDX-License-Identifier: Apache-2.0
 * Optional H4 diagnostic hook. Count real packet traffic without storing
 * Bluetooth payloads or depending on the full framework's snoop-file service.
 * This hook does not create, modify or acknowledge any HCI packets.
 */
#include <stdint.h>
#include <stdio.h>
static uint32_t incoming,outgoing;
__attribute__((weak)) void btsnoop_log_capture(uint8_t receive,uint8_t *data,uint32_t length)
{
  if(!data || !length)return;
  if(receive)__atomic_fetch_add(&incoming,1,__ATOMIC_RELAXED);
  else __atomic_fetch_add(&outgoing,1,__ATOMIC_RELAXED);
  if(!receive && length>=4 && data[0]==1)
    printf("[hci-command] op=%02x%02x params=%u\n",data[2],data[1],data[3]);
  if(length>=10 && data[0]==2) {
    unsigned handle=data[1]|((data[2]&15)<<8);
    unsigned cid=data[7]|(data[8]<<8);
    printf("[hci-acl] %s handle=%u bytes=%lu cid=%u opcode=%02x\n",
           receive?"rx":"tx",handle,(unsigned long)length,cid,data[9]);
  }
  /* Metadata only: expose real controller events during bring-up, never
   * task text, ATT values, recordings, or credentials. */
  if(receive && length>=3 && data[0]==4) {
    if(data[1]==0x3e && length>=4)
      printf("[hci-event] LE sub=%02x bytes=%lu\n",data[3],(unsigned long)length);
    else if(data[1]==0x05 && length>=7)
      printf("[hci-event] disconnect status=%02x reason=%02x\n",data[3],data[6]);
    else if(data[1]==0x0e && length>=7 && 1)
      printf("[hci-event] complete op=%02x%02x status=%02x\n",data[5],data[4],data[6]);
    else if(data[1]==0x0f && length>=7)
      printf("[hci-event] status op=%02x%02x status=%02x\n",data[6],data[5],data[3]);
  }
}
void focus_ble_hci_stats(void)
{
  printf("[ble-hci] host_tx=%lu host_rx=%lu\n",
         (unsigned long)__atomic_load_n(&outgoing,__ATOMIC_RELAXED),
         (unsigned long)__atomic_load_n(&incoming,__ATOMIC_RELAXED));
}
