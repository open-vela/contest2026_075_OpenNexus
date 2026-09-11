/****************************************************************************
 * app/focusmate/ui/focus_ui.c
 *
 * FocusMate LVGL UI (LVGL 9.1 / NuttX port).
 *
 * FocusMate owns the complete display stack here:
 *
 *   focus_ui_init()     lv_init() + lv_nuttx_init() against /dev/lcd0
 *                       (and /dev/input0 when a touch panel is present),
 *                       builds the widget tree, then spawns a background
 *                       thread that drives lv_timer_handler().
 *   focus_ui_refresh()  thread-safe widget refresh from the state machine.
 *   focus_ui_notice()   thread-safe one-line notice.
 *   focus_ui_deinit()   stop the refresh thread (the display stays up).
 *
 * A missing touchscreen is NOT fatal: the port returns a NULL indev and the
 * UI still renders, it is simply read-only.  That keeps FocusMate usable on
 * boards whose touch panel is absent or not yet brought up.
 *
 * ---------------------------------------------------------------------------
 * On-screen text and the SimSun 16 CJK font
 * ---------------------------------------------------------------------------
 * LVGL's built-in SimSun font only carries ~1100 CJK glyphs (an ancient
 * radical-oriented subset), so a naive Chinese UI renders half of its text
 * as LVGL's "missing glyph" placeholder box.  Every string below is written
 * using only characters that font actually contains, and the stage titles in
 * focus_agent.c's local planner are constrained the same way.  The strings
 * deliberately favour digits and ASCII (e.g. "02:14") over units such as
 * miao/fenzhong, which the font does not carry.
 *
 * Full-width punctuation is not safe either: U+FF01 ("!") is absent from the
 * font while U+FF0C (",") happens to be present, so ASCII punctuation is used
 * throughout.  Re-run the character-coverage check before adding any new
 * on-screen string, otherwise it silently renders as a placeholder box.
 ****************************************************************************/

#include "focus_ui.h"

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#include <lvgl/lvgl.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#ifndef CONFIG_FOCUSMATE_LCD_DEVPATH
#  define CONFIG_FOCUSMATE_LCD_DEVPATH   "/dev/lcd0"
#endif

#ifndef CONFIG_FOCUSMATE_INPUT_DEVPATH
#  define CONFIG_FOCUSMATE_INPUT_DEVPATH "/dev/input0"
#endif

/* Cap the lv_timer_handler() idle sleep so the refresh thread keeps
 * noticing widget updates queued by the CLI thread promptly.
 */

#define UI_IDLE_MAX_MS   50

/* LVGL rendering + the LCD flush callback need a generous stack. */

#define UI_THREAD_STACK  16384

/* LVGL's build globs every src/font/*.c into liblvgl.a, so the SimSun CJK
 * font object is always compiled and archived even when the Kconfig switch
 * that would declare it is not honoured by this board's defconfig.
 * Declaring it here and referencing it makes the linker pull the object out
 * of the archive, which is what actually gives us Chinese glyphs on screen
 * (without it LVGL falls back to the "missing glyph" placeholder box).
 */

extern const lv_font_t lv_font_simsun_16_cjk;

#define FONT_CJK  (&lv_font_simsun_16_cjk)

#define FM_STATE_COUNT  8

/****************************************************************************
 * Private Data
 ****************************************************************************/

static lv_obj_t *s_title_label;
static lv_obj_t *s_state_label;
static lv_obj_t *s_bar;
static lv_obj_t *s_info_label;

static pthread_mutex_t s_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_t s_thread;
static volatile int s_thread_run;
static volatile int s_ready;
static int s_touch_ok;

static const uint32_t s_state_colors[FM_STATE_COUNT] = {
  0x808080, /* IDLE        grey   */
  0xf0a000, /* PLANNING    amber  */
  0x30a030, /* READY       green  */
  0x2070d0, /* FOCUSING    blue   */
  0xd08020, /* PAUSED      orange */
  0xd02020, /* INTERRUPTED red    */
  0x4090d0, /* RECOVERING  sky    */
  0x20a060  /* COMPLETED   teal   */
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/* Background thread: drives the LVGL timer/refresh machinery.  Every LVGL
 * call is serialised behind s_lock so the CLI thread can safely refresh
 * widgets from the state-machine callbacks.
 */

static void *ui_thread(void *arg)
{
  (void)arg;

  while (s_thread_run)
    {
      uint32_t idle;

      pthread_mutex_lock(&s_lock);
      idle = lv_timer_handler();
      pthread_mutex_unlock(&s_lock);

      if (idle == 0)
        {
          idle = 1;
        }
      else if (idle > UI_IDLE_MAX_MS)
        {
          idle = UI_IDLE_MAX_MS;
        }

      usleep(idle * 1000);
    }

  return NULL;
}

/* Build the widget tree.  Must be called with s_lock held. */

static void ui_build(void)
{
  lv_obj_t *scr = lv_scr_act();

  lv_obj_set_style_bg_color(scr, lv_color_hex(0x101418), 0);

  s_title_label = lv_label_create(scr);
  lv_obj_set_style_text_font(s_title_label, &lv_font_montserrat_32, 0);
  lv_obj_set_style_text_color(s_title_label, lv_color_hex(0xffffff), 0);
  lv_label_set_text(s_title_label, "FocusMate");
  lv_obj_align(s_title_label, LV_ALIGN_TOP_MID, 0, 18);

  s_state_label = lv_label_create(scr);
  lv_obj_set_style_text_font(s_state_label, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(s_state_label, lv_color_hex(0x808080), 0);
  lv_label_set_text(s_state_label, "IDLE");
  lv_obj_align(s_state_label, LV_ALIGN_TOP_MID, 0, 66);

  /* Stage progress bar. */

  s_bar = lv_bar_create(scr);
  lv_obj_set_size(s_bar, 330, 14);
  lv_obj_align(s_bar, LV_ALIGN_TOP_MID, 0, 104);
  lv_bar_set_range(s_bar, 0, 100);
  lv_bar_set_value(s_bar, 0, LV_ANIM_OFF);
  lv_obj_add_flag(s_bar, LV_OBJ_FLAG_HIDDEN);

  s_info_label = lv_label_create(scr);
  lv_obj_set_style_text_font(s_info_label, FONT_CJK, 0);
  lv_obj_set_style_text_color(s_info_label, lv_color_hex(0xd8dee9), 0);
  lv_obj_set_style_text_align(s_info_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_line_space(s_info_label, 6, 0);
  lv_label_set_long_mode(s_info_label, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(s_info_label, 360);
  lv_obj_align(s_info_label, LV_ALIGN_TOP_MID, 0, 140);
  lv_label_set_text(s_info_label, "待机中");
}

/* Update every widget from the session.  Must be called with s_lock held. */

static void ui_apply(const fm_session_t *sess)
{
  char buf[320];
  int pct = 0;
  int stage_total = 0;
  int remain = 0;
  uint32_t color = s_state_colors[0];

  if (sess->state < FM_STATE_COUNT)
    {
      color = s_state_colors[sess->state];
    }

  lv_obj_set_style_text_color(s_state_label, lv_color_hex(color), 0);
  lv_label_set_text(s_state_label, fm_state_name(sess->state));

  if (sess->current_stage >= 0 && sess->current_stage < sess->stage_count)
    {
      stage_total = sess->stages[sess->current_stage].minutes * 60;
      remain = stage_total - sess->stage_elapsed_seconds;
      if (remain < 0)
        {
          remain = 0;
        }
      if (stage_total > 0)
        {
          pct = (sess->stage_elapsed_seconds * 100) / stage_total;
          if (pct > 100)
            {
              pct = 100;
            }
        }
    }

  switch (sess->state)
    {
    case FM_IDLE:
      lv_obj_add_flag(s_bar, LV_OBJ_FLAG_HIDDEN);
      snprintf(buf, sizeof(buf),
               "把手机放好\n想好要做的事\n来一次专注");
      lv_label_set_text(s_info_label, buf);
      break;

    case FM_PLANNING:
      lv_obj_add_flag(s_bar, LV_OBJ_FLAG_HIDDEN);
      lv_label_set_text(s_info_label, "AI 正在安排...");
      break;

    case FM_READY:
      lv_obj_add_flag(s_bar, LV_OBJ_FLAG_HIDDEN);
      snprintf(buf, sizeof(buf),
               "%s\n共 %d 分 / %d 段\n> start",
               sess->goal[0] ? sess->goal : "(空)",
               sess->total_minutes, sess->stage_count);
      lv_label_set_text(s_info_label, buf);
      break;

    case FM_FOCUSING:
    case FM_PAUSED:
      lv_obj_clear_flag(s_bar, LV_OBJ_FLAG_HIDDEN);
      lv_bar_set_value(s_bar, pct, LV_ANIM_OFF);
      if (sess->current_stage >= 0 && sess->current_stage < sess->stage_count)
        {
          snprintf(buf, sizeof(buf),
                   "%d/%d  %s\n余 %02d:%02d   %d%%\n"
                   "已专注 %02d:%02d  中断 %d 次",
                   sess->current_stage + 1, sess->stage_count,
                   sess->stages[sess->current_stage].title,
                   remain / 60, remain % 60, pct,
                   sess->elapsed_seconds / 60, sess->elapsed_seconds % 60,
                   sess->interrupt_count);
        }
      else
        {
          snprintf(buf, sizeof(buf), "已专注 %02d:%02d\n中断 %d 次",
                   sess->elapsed_seconds / 60, sess->elapsed_seconds % 60,
                   sess->interrupt_count);
        }
      lv_label_set_text(s_info_label, buf);
      break;

    case FM_INTERRUPTED:
      /* Active scene: the phone was taken away, focus auto-paused. */
      lv_obj_clear_flag(s_bar, LV_OBJ_FLAG_HIDDEN);
      lv_bar_set_value(s_bar, pct, LV_ANIM_OFF);
      if (sess->current_stage >= 0 && sess->current_stage < sess->stage_count)
        {
          snprintf(buf, sizeof(buf),
                   "手机被取走\n专注已停，已保存\n"
                   "当前: %s\n中断 %d 次\n放回手机就可接着做",
                   sess->stages[sess->current_stage].title,
                   sess->interrupt_count);
        }
      else
        {
          snprintf(buf, sizeof(buf),
                   "手机被取走\n专注已停\n中断 %d 次",
                   sess->interrupt_count);
        }
      lv_label_set_text(s_info_label, buf);
      break;

    case FM_RECOVERING:
      /* Active scene: the phone came back, proactively offer to resume. */
      lv_obj_clear_flag(s_bar, LV_OBJ_FLAG_HIDDEN);
      lv_bar_set_value(s_bar, pct, LV_ANIM_OFF);
      if (sess->current_stage >= 0 && sess->current_stage < sess->stage_count)
        {
          snprintf(buf, sizeof(buf),
                   "你回来了!手机已放回\n之前在做: %s\n"
                   "> resume 接着做",
                   sess->stages[sess->current_stage].title);
        }
      else
        {
          snprintf(buf, sizeof(buf),
                   "你回来了!手机已放回\n> resume 接着做");
        }
      lv_label_set_text(s_info_label, buf);
      break;

    case FM_COMPLETED:
      lv_obj_clear_flag(s_bar, LV_OBJ_FLAG_HIDDEN);
      lv_bar_set_value(s_bar, 100, LV_ANIM_OFF);
      snprintf(buf, sizeof(buf),
               "全部完成!\n共专注 %02d:%02d\n中断 %d 次",
               sess->elapsed_seconds / 60, sess->elapsed_seconds % 60,
               sess->interrupt_count);
      lv_label_set_text(s_info_label, buf);
      break;

    default:
      break;
    }
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int focus_ui_init(void)
{
  pthread_attr_t attr;
  lv_nuttx_dsc_t dsc;
  lv_nuttx_result_t res;

  pthread_mutex_lock(&s_lock);

  if (!s_ready)
    {
      if (!lv_is_initialized())
        {
          memset(&res, 0, sizeof(res));

          lv_init();

          lv_nuttx_dsc_init(&dsc);
          dsc.fb_path = CONFIG_FOCUSMATE_LCD_DEVPATH;
          dsc.input_path = CONFIG_FOCUSMATE_INPUT_DEVPATH;

          lv_nuttx_init(&dsc, &res);

          if (res.disp == NULL)
            {
              syslog(LOG_ERR,
                     "[focus_ui] display init failed on %s\n",
                     CONFIG_FOCUSMATE_LCD_DEVPATH);
              pthread_mutex_unlock(&s_lock);
              return -ENODEV;
            }

          s_touch_ok = (res.indev != NULL);
          syslog(LOG_INFO, "[focus_ui] %s ready, touch %s\n",
                 CONFIG_FOCUSMATE_LCD_DEVPATH,
                 s_touch_ok ? "ready" : "absent (read-only UI)");
        }

      ui_build();
      s_ready = 1;
    }

  if (!s_thread_run)
    {
      s_thread_run = 1;

      pthread_attr_init(&attr);
      pthread_attr_setstacksize(&attr, UI_THREAD_STACK);

      if (pthread_create(&s_thread, &attr, ui_thread, NULL) != 0)
        {
          s_thread_run = 0;
          pthread_attr_destroy(&attr);
          pthread_mutex_unlock(&s_lock);
          syslog(LOG_ERR, "[focus_ui] refresh thread create failed\n");
          return -EAGAIN;
        }

      pthread_attr_destroy(&attr);
    }

  pthread_mutex_unlock(&s_lock);

  syslog(LOG_INFO, "[focus_ui] initialised (touch=%d)\n", s_touch_ok);
  return 0;
}

void focus_ui_refresh(const fm_session_t *sess)
{
  if (sess == NULL)
    {
      return;
    }

  pthread_mutex_lock(&s_lock);

  if (s_ready)
    {
      ui_apply(sess);
    }

  pthread_mutex_unlock(&s_lock);
}

void focus_ui_notice(const char *msg)
{
  if (msg == NULL)
    {
      return;
    }

  pthread_mutex_lock(&s_lock);

  if (s_ready)
    {
      lv_label_set_text(s_info_label, msg);
    }

  pthread_mutex_unlock(&s_lock);
}

void focus_ui_deinit(void)
{
  pthread_mutex_lock(&s_lock);

  if (s_thread_run)
    {
      s_thread_run = 0;
      pthread_mutex_unlock(&s_lock);
      pthread_join(s_thread, NULL);
      return;
    }

  pthread_mutex_unlock(&s_lock);
}
