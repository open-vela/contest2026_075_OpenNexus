/* SPDX-License-Identifier: Apache-2.0
 * Application PCM client. Uses only the openvela audio ioctl/MQ interface;
 * no chip registers or HAL calls. Manual endpoint; silence never stops it.
 */
#include <nuttx/config.h>
#include <nuttx/audio/audio.h>
#include <sys/ioctl.h>
#include <pthread.h>
#include <mqueue.h>
#include <semaphore.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include "focus_audio.h"
#define RATE 16000u
#define MAX_BYTES (RATE * 2u * 60u)
#define NBUFS 4
#define BUFSIZE 2048
struct stream {
  int fd;
  mqd_t mq;
  char name[40];
  struct ap_buffer_s *buffers[NBUFS];
  bool reserved, started;
  size_t offset;
};
static pthread_mutex_t state_lock = PTHREAD_MUTEX_INITIALIZER;
static struct focus_audio_status status;
static uint8_t *pcm;
static uint64_t absolute_sum;
static pthread_t thread;
static sem_t initialized;
static bool sem_ready, created, stopping, keep_recording;
static unsigned generation;
static bool last_capture;

static uint64_t clock_ms(void)
{
  struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t);
  return (uint64_t)t.tv_sec*1000u+t.tv_nsec/1000000u;
}
void focus_audio_get(struct focus_audio_status *out)
{
  pthread_mutex_lock(&state_lock); *out = status; pthread_mutex_unlock(&state_lock);
}
static bool stop_requested(void)
{
  pthread_mutex_lock(&state_lock); bool stop = stopping; pthread_mutex_unlock(&state_lock);
  return stop;
}
static int enqueue(struct stream *s,struct ap_buffer_s *apb)
{
  struct audio_buf_desc_s desc = {0};
  desc.numbytes = apb->nmaxbytes; desc.u.buffer = apb;
  if (ioctl(s->fd,AUDIOIOC_ENQUEUEBUFFER,(uintptr_t)&desc) < 0) return -errno;
  return 0;
}
static int open_stream(struct stream *s,bool capture)
{
  memset(s,0,sizeof(*s)); s->fd = -1; s->mq = (mqd_t)-1;
  s->fd = open(capture ? "/dev/audio/pcm0c" : "/dev/audio/pcm0p",O_RDWR);
  if (s->fd < 0) return -errno;
  if (ioctl(s->fd,AUDIOIOC_RESERVE,0) < 0) return -errno;
  s->reserved = true;
  struct audio_caps_desc_s caps = {0};
  caps.caps.ac_len = sizeof(caps.caps);
  caps.caps.ac_type = capture ? AUDIO_TYPE_INPUT : AUDIO_TYPE_OUTPUT;
  caps.caps.ac_subtype = AUDIO_FMT_PCM; caps.caps.ac_channels = 1;
  caps.caps.ac_controls.hw[0] = RATE; caps.caps.ac_controls.b[2] = 16;
  if (ioctl(s->fd,AUDIOIOC_CONFIGURE,(uintptr_t)&caps) < 0) return -errno;
  struct ap_buffer_info_s info = {0};
  if (ioctl(s->fd,AUDIOIOC_GETBUFFERINFO,(uintptr_t)&info) < 0) return -errno;
  snprintf(s->name,sizeof(s->name),"/fmpcm%lx_%u",(unsigned long)(uintptr_t)s,++generation);
  struct mq_attr attr = {0}; attr.mq_maxmsg = 16; attr.mq_msgsize = sizeof(struct audio_msg_s);
  s->mq = mq_open(s->name,O_CREAT|O_EXCL|O_RDWR|O_NONBLOCK,0600,&attr);
  if (s->mq == (mqd_t)-1) return -errno;
  if (ioctl(s->fd,AUDIOIOC_REGISTERMQ,(unsigned long)s->mq) < 0) return -errno;
  for (unsigned i = 0; i < NBUFS; ++i) {
    struct audio_buf_desc_s desc = {0};
    desc.numbytes = BUFSIZE; desc.u.pbuffer = &s->buffers[i];
    int ret = ioctl(s->fd,AUDIOIOC_ALLOCBUFFER,(uintptr_t)&desc);
    if (ret != sizeof(desc)) return ret < 0 ? -errno : -EIO;
  }
  return 0;
}
static int receive_pcm(struct ap_buffer_s *apb)
{
  if (!apb->nbytes) return 0;
  if (apb->nbytes > apb->nmaxbytes || (apb->nbytes & 1u)) return -EIO;
  pthread_mutex_lock(&state_lock);
  if (status.bytes + apb->nbytes > MAX_BYTES) {
    status.limit = true; pthread_mutex_unlock(&state_lock); return -EFBIG;
  }
  memcpy(pcm+status.bytes,apb->samp,apb->nbytes);
  unsigned peak = 0; uint64_t sum = 0;
  int16_t *samples = (int16_t *)apb->samp;
  for (unsigned i = 0; i < apb->nbytes/2; ++i) {
    int value = samples[i]; unsigned amp = value < 0 ? -value : value;
    if (amp > peak) peak = amp;
    sum += amp;
  }
  unsigned average = sum/(apb->nbytes/2);
  if (average > 50 && peak > 300) status.signal = true;
  if (peak > status.peak) status.peak = peak;
  absolute_sum += sum; status.bytes += apb->nbytes;
  status.milliseconds = status.bytes*1000u/(RATE*2u);
  status.mean_abs = absolute_sum/(status.bytes/2u);
  memmove(status.levels,status.levels+1,FOCUS_AUDIO_COLUMNS-1);
  unsigned level = average > 40 ? (average-40)*100/600 : 0;
  status.levels[FOCUS_AUDIO_COLUMNS-1] = level > 100 ? 100 : level;
  pthread_mutex_unlock(&state_lock);
  return 0;
}
static int fill_output(struct stream *s,struct ap_buffer_s *apb)
{
  pthread_mutex_lock(&state_lock);
  size_t remaining = status.bytes - s->offset;
  if (!remaining) { pthread_mutex_unlock(&state_lock); return 0; }
  size_t count = remaining < apb->nmaxbytes ? remaining : apb->nmaxbytes;
  count &= ~3u;
  if (!count) { pthread_mutex_unlock(&state_lock); return -EINVAL; }
  /* Playback gain is applied only to the outgoing copy; raw PCM is retained. */
  int16_t *source = (int16_t *)(pcm+s->offset), *dest = (int16_t *)apb->samp;
  for (size_t i = 0; i < count/2; ++i) {
    int value = (int)source[i]*4;
    dest[i] = value < -32768 ? -32768 : value > 32767 ? 32767 : value;
  }
  s->offset += count;
  apb->nbytes = count; apb->curbyte = 0;
  apb->flags = s->offset == status.bytes ? AUDIO_APB_FINAL : 0;
  pthread_mutex_unlock(&state_lock);
  int ret = enqueue(s,apb);
  return ret < 0 ? ret : 1;
}
static void stop_stream(struct stream *s)
{
  if (s->started) {
    ioctl(s->fd,AUDIOIOC_STOP,0); s->started = false;
  }
}
static void close_stream(struct stream *s)
{
  stop_stream(s);
  if (s->fd >= 0) {
    if (s->mq != (mqd_t)-1) ioctl(s->fd,AUDIOIOC_UNREGISTERMQ,(unsigned long)s->mq);
    /* RELEASE also flushes queued buffers if START failed. Release driver
     * ownership before dropping the client's allocation references. */
    if (s->reserved) { ioctl(s->fd,AUDIOIOC_RELEASE,0); s->reserved = false; }
    for (unsigned i = 0; i < NBUFS; ++i) if (s->buffers[i]) {
      struct audio_buf_desc_s desc = {0};desc.u.buffer = s->buffers[i];
      ioctl(s->fd,AUDIOIOC_FREEBUFFER,(uintptr_t)&desc);
    }
    close(s->fd);
  }
  if (s->mq != (mqd_t)-1) mq_close(s->mq);
  if (s->name[0]) mq_unlink(s->name);
}

static void *pcm_worker(void *arg)
{
  bool capture = (uintptr_t)arg != 0;
  struct stream s;
  int ret = open_stream(&s,capture);
  if (ret < 0) goto ready;
  for (unsigned i = 0; i < NBUFS; ++i) {
    if (capture) {
      s.buffers[i]->nbytes = s.buffers[i]->nmaxbytes;
      s.buffers[i]->flags = 0;
      ret = enqueue(&s,s.buffers[i]);
    } else ret = fill_output(&s,s.buffers[i]);
    if (ret < 0) goto ready;
  }
  if (ioctl(s.fd,AUDIOIOC_START,0) < 0) { ret = -errno; goto ready; }
  s.started = true;
  ret = 0;
ready:
  pthread_mutex_lock(&state_lock); status.error = ret; pthread_mutex_unlock(&state_lock);
  sem_post(&initialized);
  uint64_t last_frame = clock_ms();
  bool complete = false;
  while (!ret && !complete && !stop_requested()) {
    struct audio_msg_s msg;
    ssize_t n = mq_receive(s.mq,(char *)&msg,sizeof(msg),NULL);
    if (n < 0) {
      if (errno != EAGAIN && errno != EINTR) { ret = -errno; break; }
      if (clock_ms()-last_frame > 2000) { ret = -ETIMEDOUT; break; }
      usleep(5000); continue;
    }
    if (n != sizeof(msg)) { ret = -EIO; break; }
    last_frame = clock_ms();
    if (msg.msg_id == AUDIO_MSG_DEQUEUE && msg.u.ptr) {
      struct ap_buffer_s *apb = msg.u.ptr;
      if (capture) {
        ret = receive_pcm(apb);
        if (!ret && !stop_requested()) {
          apb->flags = 0; apb->nbytes = apb->nmaxbytes; ret = enqueue(&s,apb);
        }
      } else {
        int filled = fill_output(&s,apb);
        if (filled < 0) ret = filled;
      }
    } else if (msg.msg_id == AUDIO_MSG_COMPLETE) complete = true;
    else if (msg.msg_id == AUDIO_MSG_IOERR) ret = -EIO;
  }
  stop_stream(&s);
  /* STOP returns every queued buffer before it returns. Preserve a partially
   * filled capture buffer at the manual endpoint, then release all resources. */
  if (capture && s.mq != (mqd_t)-1) {
    struct audio_msg_s msg;
    while (mq_receive(s.mq,(char *)&msg,sizeof(msg),NULL) == sizeof(msg))
      if (msg.msg_id == AUDIO_MSG_DEQUEUE && msg.u.ptr) {
        int got = receive_pcm(msg.u.ptr);
        if (got < 0 && !ret) ret = got;
      }
  }
  close_stream(&s);
  pthread_mutex_lock(&state_lock);
  status.active = status.playing = false;
  status.error = ret;
  if (capture) {
    if (!keep_recording) { status.bytes = 0; status.signal = false; }
    status.ready = status.bytes > 0 && status.signal;
  }
  pthread_mutex_unlock(&state_lock);
  focus_audio_print();
  return NULL;
}

int focus_audio_end(bool keep)
{
  pthread_mutex_lock(&state_lock); stopping = true; keep_recording = keep;
  pthread_mutex_unlock(&state_lock);
  if (created) { pthread_join(thread,NULL); created = false; }
  if (!keep && last_capture) {
    pthread_mutex_lock(&state_lock); status.bytes = 0; status.ready = false;
    status.signal = false; pthread_mutex_unlock(&state_lock);
  }
  return status.error;
}
static int launch(bool capture)
{
  if (!sem_ready) { sem_init(&initialized,0,0); sem_ready = true; }
  while (sem_trywait(&initialized) == 0) { }
  pthread_mutex_lock(&state_lock);
  stopping = false; keep_recording = true; status.error = 0;
  status.active = capture; status.playing = !capture; last_capture = capture;
  pthread_mutex_unlock(&state_lock);
  pthread_attr_t attr;
  pthread_attr_init(&attr); pthread_attr_setstacksize(&attr,8192);
  int ret = pthread_create(&thread,&attr,pcm_worker,(void *)(uintptr_t)capture);
  pthread_attr_destroy(&attr);
  if (ret) {
    pthread_mutex_lock(&state_lock); status.active = status.playing = false;
    status.error = -ret; pthread_mutex_unlock(&state_lock); return -ret;
  }
  created = true;
  struct timespec until; clock_gettime(CLOCK_REALTIME,&until); until.tv_sec += 3;
  do { ret = sem_timedwait(&initialized,&until); } while (ret < 0 && errno == EINTR);
  if (ret < 0) { ret = -errno; focus_audio_end(false); return ret; }
  struct focus_audio_status now; focus_audio_get(&now);
  return now.error;
}
int focus_audio_begin(void)
{
  struct focus_audio_status now; focus_audio_get(&now);
  if (now.active || now.playing) return -EBUSY;
  focus_audio_end(true);
  uint8_t *new_pcm = malloc(MAX_BYTES);
  if (!new_pcm) return -ENOMEM;
  pthread_mutex_lock(&state_lock);
  free(pcm); pcm = new_pcm; memset(&status,0,sizeof(status)); absolute_sum = 0;
  pthread_mutex_unlock(&state_lock);
  return launch(true);
}
int focus_audio_play(void)
{
  struct focus_audio_status now; focus_audio_get(&now);
  if (now.active || now.playing) return -EBUSY;
  if (!now.ready) return -ENODATA;
  focus_audio_end(true);
  return launch(false);
}
void focus_audio_print(void)
{
  struct focus_audio_status now; focus_audio_get(&now);
  printf("[pcm-client] active=%d playing=%d ready=%d signal=%d bytes=%lu ms=%u peak=%u mean=%u error=%d limit=%d\n",
         now.active,now.playing,now.ready,now.signal,(unsigned long)now.bytes,
         now.milliseconds,now.peak,now.mean_abs,now.error,now.limit);
  fflush(stdout);
}
int focus_audio_dump(void)
{
  static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  pthread_mutex_lock(&state_lock);
  if (status.active || status.playing || !status.bytes) {
    pthread_mutex_unlock(&state_lock); return -EBUSY;
  }
  uint32_t hash = 2166136261u;
  for (size_t i = 0; i < status.bytes; ++i) hash = (hash ^ pcm[i])*16777619u;
  printf("MIC_PCM_BEGIN samples=%lu rate=%u bytes=%lu fnv1a=%08lx\n",
         (unsigned long)status.bytes/2,RATE,(unsigned long)status.bytes,(unsigned long)hash);
  for (size_t i = 0; i < status.bytes; i += 48) {
    size_t count = status.bytes-i < 48 ? status.bytes-i : 48;
    char line[65]; size_t used = 0;
    for (size_t j = 0; j < count; j += 3) {
      unsigned a = pcm[i+j], b = j+1 < count ? pcm[i+j+1] : 0,
               c = j+2 < count ? pcm[i+j+2] : 0;
      line[used++] = alphabet[a>>2]; line[used++] = alphabet[((a&3)<<4)|(b>>4)];
      line[used++] = j+1 < count ? alphabet[((b&15)<<2)|(c>>6)] : '=';
      line[used++] = j+2 < count ? alphabet[c&63] : '=';
    }
    line[used] = 0; puts(line);
  }
  puts("MIC_PCM_END"); fflush(stdout);
  pthread_mutex_unlock(&state_lock);
  return 0;
}
