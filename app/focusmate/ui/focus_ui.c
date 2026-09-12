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
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

#include <nuttx/input/buttons.h>

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

static const char *focus_ui_fbdev_path(void)
{
  if (access(CONFIG_FOCUSMATE_LCD_DEVPATH, F_OK) == 0)
    {
      return CONFIG_FOCUSMATE_LCD_DEVPATH;
    }

  /* goldfish exposes /dev/fb0, while the SF32LB52 LCD uses /dev/lcd0. */
  if (strcmp(CONFIG_FOCUSMATE_LCD_DEVPATH, "/dev/fb0") != 0 &&
      access("/dev/fb0", F_OK) == 0)
    {
      return "/dev/fb0";
    }

  return CONFIG_FOCUSMATE_LCD_DEVPATH;
}

/* Cap the lv_timer_handler() idle sleep so the refresh thread keeps
 * noticing widget updates queued by the CLI thread promptly.
 */

#define UI_IDLE_MAX_MS   50

/* ---------------------------------------------------------------------------
 * Board key fallback
 * ---------------------------------------------------------------------------
 * This panel is a display-only module (SiFli describe it as a "Single-Screen
 * LCD"): it carries no touch layer, and no touch controller answers on either
 * I2C bus.  /dev/input0 therefore exists - sifli's I2C driver reports success
 * even when the chip NAKs - but it can never deliver an event.
 *
 * The board's single user key (KEY2 on PA11, exposed as /dev/buttons) drives
 * the same command queue the on-screen buttons feed:
 *
 *   short press -> the primary action for the current state
 *                  (START / PAUSE / RESUME)
 *   long press  -> STOP, i.e. give up the session
 */

#define BTN_DEVPATH        "/dev/buttons"
#define BTN_POLL_MS        100
#define BTN_LONGPRESS_MS   1500

/* LVGL rendering + the LCD flush callback need a generous stack. */

#define UI_THREAD_STACK  16384

/* LVGL's build globs every src/font/<name>.c into liblvgl.a, so font objects are
 * always archived even when the Kconfig switch that would declare them is not
 * honoured by this board's defconfig.
 *
 * We generate our own 24px SimSun (see docs/board_bringup) because the
 * built-in lv_font_simsun_16_cjk is both too small to read comfortably on the
 * 390x450 panel and limited to ~1100 glyphs.  The generated font carries
 * ASCII plus the 3755 GB2312 level-1 hanzi, so free-form goals typed on the
 * console render too.
 */

extern const lv_font_t lv_font_simsun_24_cjk;

#define FONT_CJK  (&lv_font_simsun_24_cjk)

#define FM_STATE_COUNT  11

/****************************************************************************
 * Private Data
 ****************************************************************************/

static lv_obj_t *s_title_label;
static lv_obj_t *s_state_label;
static lv_obj_t *s_timer_label;
static lv_obj_t *s_bar;
static lv_obj_t *s_info_label;
static lv_obj_t *s_btn_primary;
static lv_obj_t *s_btn_primary_lbl;
static lv_obj_t *s_btn_stop;
static lv_obj_t *s_btn_stop_lbl;

static lv_obj_t *s_review_overlay;
static lv_obj_t *s_review_title;
static lv_obj_t *s_review_task_btns[FM_MAX_STAGES];
static lv_obj_t *s_review_task_lbls[FM_MAX_STAGES];
static int s_review_mode;
static int s_review_selection;

static lv_obj_t *s_duration_overlay;
static lv_obj_t *s_duration_task_label;
static lv_obj_t *s_duration_btns[4];
static int s_duration_selected;

/* Button requests cross from the refresh thread to the CLI thread here.
 * This is deliberately a separate lock: the LVGL callback already runs
 * inside ui_thread() while s_lock is held, so reusing s_lock would
 * self-deadlock on the first tap.
 */

static pthread_mutex_t s_cmd_lock = PTHREAD_MUTEX_INITIALIZER;
static fm_ui_cmd_t s_pending_cmd = FM_UI_CMD_NONE;

static pthread_t s_btn_thread;
static volatile int s_btn_thread_run;
static volatile int s_cur_state = FM_IDLE;

static pthread_mutex_t s_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_t s_thread;
static volatile int s_thread_run;
static volatile int s_ready;
static int s_touch_ok;

static const uint32_t s_state_colors[FM_STATE_COUNT] = {
  0x808080, /* IDLE            grey   */
  0xf0a000, /* PLANNING        amber  */
  0x30a030, /* READY           green  */
  0x20a060, /* DURATION        teal   */
  0x2070d0, /* FOCUSING        blue   */
  0xd08020, /* PAUSED          orange */
  0xd02020, /* INTERRUPTED     red    */
  0x4090d0, /* RECOVERING      sky    */
  0xf0a000, /* REVIEWING       amber  */
  0xd02020, /* SETTLE_CONFIRM  red    */
  0x20a060  /* COMPLETED       teal   */
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

/* Record a button tap.  Runs on the refresh thread, deep inside
 * lv_timer_handler(), so it must not touch the state machine - it only
 * leaves a request behind for the CLI thread to pick up.
 */

static void btn_event_cb(lv_event_t *e)
{
  lv_obj_t *btn = lv_event_get_target(e);
  fm_ui_cmd_t cmd = (fm_ui_cmd_t)(intptr_t)lv_obj_get_user_data(btn);

  if (cmd == FM_UI_CMD_NONE)
    {
      return;
    }

  pthread_mutex_lock(&s_cmd_lock);
  s_pending_cmd = cmd;
  pthread_mutex_unlock(&s_cmd_lock);
}

static void review_task_cb(lv_event_t *e)
{
  lv_obj_t *btn = lv_event_get_target(e);
  int idx = (int)(intptr_t)lv_obj_get_user_data(btn);

  pthread_mutex_lock(&s_cmd_lock);
  s_review_selection = idx;
  s_pending_cmd = FM_UI_CMD_REVIEW_SELECTED;
  pthread_mutex_unlock(&s_cmd_lock);
}

static void duration_cb(lv_event_t *e)
{
  lv_obj_t *btn = lv_event_get_target(e);

  pthread_mutex_lock(&s_cmd_lock);
  s_duration_selected = (int)(intptr_t)lv_obj_get_user_data(btn);
  s_pending_cmd = FM_UI_CMD_DURATION_SELECTED;
  pthread_mutex_unlock(&s_cmd_lock);
}

/* Show/hide a button, retitle it and re-point it at a command.  Must be
 * called with s_lock held.  The primary button carries START, PAUSE or
 * RESUME depending on the state, so the command lives in user_data rather
 * than in the event callback.
 */

static void btn_config(lv_obj_t *btn, lv_obj_t *lbl, int visible,
                       const char *text, uint32_t bg, fm_ui_cmd_t cmd)
{
  if (!visible)
    {
      lv_obj_add_flag(btn, LV_OBJ_FLAG_HIDDEN);
      return;
    }

  if (text != NULL)
    {
      lv_label_set_text(lbl, text);
    }

  lv_obj_set_style_bg_color(btn, lv_color_hex(bg), 0);
  lv_obj_set_user_data(btn, (void *)(intptr_t)cmd);
  lv_obj_clear_flag(btn, LV_OBJ_FLAG_HIDDEN);
}

static lv_obj_t *btn_make(lv_obj_t *parent,
                          lv_align_t align, lv_coord_t x_ofs,
                          lv_obj_t **out_lbl)
{
  lv_obj_t *btn = lv_button_create(parent);
  lv_obj_t *lbl;

  lv_obj_set_size(btn, 140, 56);
  lv_obj_align(btn, align, x_ofs, -44);
  lv_obj_set_style_radius(btn, 14, 0);
  lv_obj_set_style_bg_color(btn, lv_color_hex(0x2a3138), 0);
  lv_obj_add_event_cb(btn, btn_event_cb, LV_EVENT_CLICKED, NULL);

  lbl = lv_label_create(btn);
  lv_obj_set_style_text_font(lbl, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(lbl, lv_color_hex(0xffffff), 0);
  lv_label_set_text(lbl, "-");
  lv_obj_center(lbl);

  lv_obj_set_user_data(btn, (void *)(intptr_t)FM_UI_CMD_NONE);
  lv_obj_add_flag(btn, LV_OBJ_FLAG_HIDDEN);

  *out_lbl = lbl;
  return btn;
}

/* Primary action offered by the current state, mirroring the on-screen
 * primary button so the key and the button never disagree. */

static fm_ui_cmd_t primary_cmd_for(int state)
{
  switch (state)
    {
    case FM_READY:
      return FM_UI_CMD_START;

    case FM_COMPLETED:
    case FM_IDLE:
      return FM_UI_CMD_QUICKSTART;

    case FM_FOCUSING:
      return FM_UI_CMD_END_ROUND;

    case FM_PAUSED:
    case FM_INTERRUPTED:
    case FM_RECOVERING:
      return FM_UI_CMD_RESUME;

    case FM_REVIEWING:
      return FM_UI_CMD_REVIEW_YES;

    case FM_SETTLE_CONFIRM:
      return FM_UI_CMD_SETTLE_CONFIRM;

    default:
      return FM_UI_CMD_NONE;
    }
}

/* Watch the board key and turn presses into queued commands.  Like the LVGL
 * callback, this thread never touches the state machine itself. */

static void *button_thread(void *arg)
{
  struct timespec t_down = {0, 0};
  bool down = false;
  int fd;

  (void)arg;

  fd = open(BTN_DEVPATH, O_RDONLY);
  if (fd < 0)
    {
      syslog(LOG_WARNING, "[focus_ui] %s unavailable: %d (key input off)\n",
             BTN_DEVPATH, errno);
      return NULL;
    }

  syslog(LOG_INFO, "[focus_ui] key input on %s (short=action, long=settle)\n",
         BTN_DEVPATH);

  while (s_btn_thread_run)
    {
      btn_buttonset_t bits = 0;
      struct pollfd pfd;

      pfd.fd = fd;
      pfd.events = POLLIN;
      pfd.revents = 0;

      if (poll(&pfd, 1, BTN_POLL_MS) <= 0)
        {
          continue;
        }

      if (read(fd, &bits, sizeof(bits)) != (ssize_t)sizeof(bits))
        {
          continue;
        }

      if (bits != 0 && !down)
        {
          down = true;
          clock_gettime(CLOCK_MONOTONIC, &t_down);
        }
      else if (bits == 0 && down)
        {
          struct timespec now;
          fm_ui_cmd_t cmd;
          long held;

          down = false;
          clock_gettime(CLOCK_MONOTONIC, &now);
          held = (now.tv_sec - t_down.tv_sec) * 1000
                 + (now.tv_nsec - t_down.tv_nsec) / 1000000;

          if (held >= BTN_LONGPRESS_MS)
            {
              cmd = (s_cur_state == FM_DURATION_SELECT)
                        ? FM_UI_CMD_DURATION_CANCEL
                        : FM_UI_CMD_SETTLE_REQUEST;
            }
          else
            {
              cmd = primary_cmd_for(s_cur_state);
            }

          if (cmd != FM_UI_CMD_NONE)
            {
              pthread_mutex_lock(&s_cmd_lock);
              s_pending_cmd = cmd;
              pthread_mutex_unlock(&s_cmd_lock);
            }
        }
    }

  close(fd);
  return NULL;
}

/* Build the widget tree.  Must be called with s_lock held. */

static void ui_build(void)
{
  lv_obj_t *scr = lv_scr_act();

  /* Never draw without a screen: lv_scr_act() returns NULL when no display is
   * registered, and every lv_obj_* call below would dereference it. */
  if (scr == NULL)
    {
      syslog(LOG_ERR, "[focus_ui] no active screen; UI not built\n");
      return;
    }

  lv_obj_set_style_bg_color(scr, lv_color_hex(0x101418), 0);

  s_title_label = lv_label_create(scr);
  lv_obj_set_style_text_font(s_title_label, &lv_font_montserrat_32, 0);
  lv_obj_set_style_text_color(s_title_label, lv_color_hex(0xffffff), 0);
  lv_label_set_text(s_title_label, "FocusMate");
  lv_obj_align(s_title_label, LV_ALIGN_TOP_MID, 0, 10);

  s_state_label = lv_label_create(scr);
  lv_obj_set_style_text_font(s_state_label, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(s_state_label, lv_color_hex(0x808080), 0);
  lv_label_set_text(s_state_label, "IDLE");
  lv_obj_align(s_state_label, LV_ALIGN_TOP_MID, 0, 52);

  /* The countdown is the one thing the user actually reads at a glance, so it
   * gets the largest type on the panel. */
  s_timer_label = lv_label_create(scr);
  lv_obj_set_style_text_font(s_timer_label, &lv_font_montserrat_40, 0);
  lv_obj_set_style_text_color(s_timer_label, lv_color_hex(0xffffff), 0);
  lv_label_set_text(s_timer_label, "");
  lv_obj_align(s_timer_label, LV_ALIGN_TOP_MID, 0, 84);
  lv_obj_add_flag(s_timer_label, LV_OBJ_FLAG_HIDDEN);

  /* Stage progress bar. */

  s_bar = lv_bar_create(scr);
  lv_obj_set_size(s_bar, 330, 16);
  lv_obj_align(s_bar, LV_ALIGN_TOP_MID, 0, 142);
  lv_bar_set_range(s_bar, 0, 100);
  lv_bar_set_value(s_bar, 0, LV_ANIM_OFF);
  lv_obj_add_flag(s_bar, LV_OBJ_FLAG_HIDDEN);

  s_info_label = lv_label_create(scr);
  lv_obj_set_style_text_font(s_info_label, FONT_CJK, 0);
  lv_obj_set_style_text_color(s_info_label, lv_color_hex(0xd8dee9), 0);
  lv_obj_set_style_text_align(s_info_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_line_space(s_info_label, 10, 0);
  lv_label_set_long_mode(s_info_label, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(s_info_label, 360);
  lv_obj_align(s_info_label, LV_ALIGN_TOP_MID, 0, 180);
  lv_label_set_text(s_info_label, "待机中");

  /* On-screen controls.  The primary button means START / PAUSE / RESUME
   * depending on the state; STOP is the "give up" action. */

  s_btn_primary = btn_make(scr, LV_ALIGN_BOTTOM_LEFT, 30, &s_btn_primary_lbl);
  s_btn_stop = btn_make(scr, LV_ALIGN_BOTTOM_RIGHT, -30, &s_btn_stop_lbl);

  s_review_overlay = lv_obj_create(scr);
  lv_obj_set_size(s_review_overlay, 360, 300);
  lv_obj_center(s_review_overlay);
  lv_obj_set_style_bg_color(s_review_overlay, lv_color_hex(0x1b222b), 0);
  lv_obj_set_style_radius(s_review_overlay, 16, 0);
  lv_obj_add_flag(s_review_overlay, LV_OBJ_FLAG_HIDDEN);

  s_review_title = lv_label_create(s_review_overlay);
  lv_obj_set_style_text_font(s_review_title, FONT_CJK, 0);
  lv_obj_set_style_text_color(s_review_title, lv_color_hex(0xffffff), 0);
  lv_label_set_text(s_review_title, "选择本轮完成的任务");
  lv_obj_align(s_review_title, LV_ALIGN_TOP_MID, 0, 8);

  for (int i = 0; i < FM_MAX_STAGES; i++)
    {
      lv_obj_t *btn = lv_button_create(s_review_overlay);
      lv_obj_set_size(btn, 320, 48);
      lv_obj_align(btn, LV_ALIGN_TOP_MID, 0, 48 + i * 58);
      lv_obj_add_flag(btn, LV_OBJ_FLAG_HIDDEN);
      lv_obj_set_user_data(btn, (void *)(intptr_t)i);
      lv_obj_add_event_cb(btn, review_task_cb, LV_EVENT_CLICKED, NULL);

      lv_obj_t *lbl = lv_label_create(btn);
      lv_obj_set_style_text_font(lbl, FONT_CJK, 0);
      lv_obj_set_style_text_color(lbl, lv_color_hex(0xffffff), 0);
      lv_label_set_text(lbl, "-");
      lv_obj_center(lbl);

      s_review_task_btns[i] = btn;
      s_review_task_lbls[i] = lbl;
    }

  /* Focus-round length selector.  The user chooses the length after START;
   * the AI plan never supplies a time budget. */
  s_duration_overlay = lv_obj_create(scr);
  lv_obj_set_size(s_duration_overlay, 360, 300);
  lv_obj_center(s_duration_overlay);
  lv_obj_set_style_bg_color(s_duration_overlay, lv_color_hex(0x1b222b), 0);
  lv_obj_set_style_radius(s_duration_overlay, 16, 0);
  lv_obj_add_flag(s_duration_overlay, LV_OBJ_FLAG_HIDDEN);

  s_duration_task_label = lv_label_create(s_duration_overlay);
  lv_obj_set_style_text_font(s_duration_task_label, FONT_CJK, 0);
  lv_obj_set_style_text_color(s_duration_task_label, lv_color_hex(0xffffff), 0);
  lv_obj_set_style_text_align(s_duration_task_label, LV_TEXT_ALIGN_LEFT, 0);
  lv_obj_set_style_text_line_space(s_duration_task_label, 2, 0);
  lv_obj_set_width(s_duration_task_label, 330);
  lv_label_set_long_mode(s_duration_task_label, LV_LABEL_LONG_WRAP);
  lv_label_set_text(s_duration_task_label, "");
  lv_obj_align(s_duration_task_label, LV_ALIGN_TOP_MID, 0, 6);

  {
    static const int duration_values[4] = {25, 30, 45, 60};

    for (int i = 0; i < 4; i++)
      {
        char text[16];
        lv_obj_t *btn = lv_button_create(s_duration_overlay);
        lv_obj_set_size(btn, 150, 60);
        lv_obj_align(btn, LV_ALIGN_TOP_LEFT,
                     20 + (i % 2) * 170,
                     122 + (i / 2) * 76);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x2a3138), 0);
        lv_obj_set_style_radius(btn, 14, 0);
        lv_obj_set_user_data(btn, (void *)(intptr_t)duration_values[i]);
        lv_obj_add_event_cb(btn, duration_cb, LV_EVENT_CLICKED, NULL);

        lv_obj_t *lbl = lv_label_create(btn);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_32, 0);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0xffffff), 0);
        snprintf(text, sizeof(text), "%dm", duration_values[i]);
        lv_label_set_text(lbl, text);
        lv_obj_center(lbl);

        s_duration_btns[i] = btn;
      }
  }
}

/* The big countdown, shown only while a session is running.  Must be called
 * with s_lock held. */

static void timer_show(const char *text)
{
  lv_label_set_text(s_timer_label, text);
  lv_obj_clear_flag(s_timer_label, LV_OBJ_FLAG_HIDDEN);
}

static void timer_hide(void)
{
  lv_obj_add_flag(s_timer_label, LV_OBJ_FLAG_HIDDEN);
}

static void review_apply(const fm_session_t *sess)
{
  if (s_review_overlay == NULL)
    {
      return;
    }

  if (sess->state != FM_REVIEWING)
    {
      s_review_mode = 0;
      s_review_selection = -1;
      lv_obj_add_flag(s_review_overlay, LV_OBJ_FLAG_HIDDEN);
      return;
    }

  if (!s_review_mode)
    {
      lv_obj_add_flag(s_review_overlay, LV_OBJ_FLAG_HIDDEN);
      return;
    }

  lv_obj_clear_flag(s_review_overlay, LV_OBJ_FLAG_HIDDEN);
  for (int i = 0; i < FM_MAX_STAGES; i++)
    {
      if (i < sess->stage_count && !sess->stages[i].completed)
        {
          char task[96];
          snprintf(task, sizeof(task), "%d. %s", i + 1, sess->stages[i].title);
          lv_label_set_text(s_review_task_lbls[i], task);
          lv_obj_clear_flag(s_review_task_btns[i], LV_OBJ_FLAG_HIDDEN);
        }
      else
        {
          lv_obj_add_flag(s_review_task_btns[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void duration_apply(const fm_session_t *sess)
{
  if (s_duration_overlay == NULL)
    {
      return;
    }

  if (sess->state == FM_DURATION_SELECT)
    {
      lv_obj_clear_flag(s_duration_overlay, LV_OBJ_FLAG_HIDDEN);
    }
  else
    {
      lv_obj_add_flag(s_duration_overlay, LV_OBJ_FLAG_HIDDEN);
    }
}

/* Update every widget from the session.  Must be called with s_lock held. */

static void ui_apply(const fm_session_t *sess)
{
  char buf[320];
  int pct = 0;
  int stage_total = 0;
  int remain = 0;
  uint32_t color = s_state_colors[0];

  /* Let the key thread know which action is the primary one right now. */
  s_cur_state = (int)sess->state;

  if (sess->state < FM_STATE_COUNT)
    {
      color = s_state_colors[sess->state];
    }

  lv_obj_set_style_text_color(s_state_label, lv_color_hex(color), 0);
  lv_label_set_text(s_state_label, fm_state_name(sess->state));

  if (sess->current_stage >= 0 && sess->current_stage < sess->stage_count)
    {
      stage_total = sess->total_minutes * 60;
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
      timer_hide();
      lv_label_set_text(s_info_label, "把手机放好\n想好要做的事\n点一下就好");
      break;

    case FM_PLANNING:
      lv_obj_add_flag(s_bar, LV_OBJ_FLAG_HIDDEN);
      timer_hide();
      lv_label_set_text(s_info_label, "AI 正在安排...");
      break;

    case FM_READY:
      lv_obj_add_flag(s_bar, LV_OBJ_FLAG_HIDDEN);
      timer_hide();
      snprintf(buf, sizeof(buf),
               "AI 已拆解为 %d 项\n点击 START 选择本轮时长",
               sess->stage_count);
      lv_label_set_text(s_info_label, buf);
      break;

    case FM_DURATION_SELECT:
      lv_obj_add_flag(s_bar, LV_OBJ_FLAG_HIDDEN);
      timer_hide();
      lv_label_set_text(s_info_label, "");
      {
        char tasks[512];
        int off = 0;
        int shown = 0;
        int remaining = 0;
        int n = sess->stage_count > FM_MAX_STAGES
                    ? FM_MAX_STAGES : sess->stage_count;

        for (int i = 0; i < n; i++)
          {
            if (!sess->stages[i].completed)
              {
                remaining++;
              }
          }

        tasks[0] = '\0';
        for (int i = 0; i < n; i++)
          {
            int wrote;

            if (sess->stages[i].completed)
              {
                continue;
              }

            wrote = snprintf(tasks + off, sizeof(tasks) - off,
                             "%d. %s%s", shown + 1, sess->stages[i].title,
                             (shown + 1 < remaining) ? "\n" : "");
            if (wrote < 0)
              {
                break;
              }
            off += wrote;
            shown++;
            if (off >= (int)sizeof(tasks))
              {
                tasks[sizeof(tasks) - 1] = '\0';
                break;
              }
          }
        lv_label_set_text(s_duration_task_label, tasks);
      }
      break;

    case FM_FOCUSING:
    case FM_PAUSED:
      lv_obj_clear_flag(s_bar, LV_OBJ_FLAG_HIDDEN);
      lv_bar_set_value(s_bar, pct, LV_ANIM_OFF);
      {
        char tbuf[16];

        snprintf(tbuf, sizeof(tbuf), "%02d:%02d", remain / 60, remain % 60);
        timer_show(tbuf);
      }
      if (sess->current_stage >= 0 && sess->current_stage < sess->stage_count)
        {
          snprintf(buf, sizeof(buf),
                   "%d/%d  %s\n%d%%\n已专注 %02d:%02d  中断 %d 次",
                   sess->current_stage + 1, sess->stage_count,
                   sess->stages[sess->current_stage].title,
                   pct,
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
      {
        char tbuf[16];

        snprintf(tbuf, sizeof(tbuf), "%02d:%02d", remain / 60, remain % 60);
        timer_show(tbuf);
      }
      if (sess->current_stage >= 0 && sess->current_stage < sess->stage_count)
        {
          snprintf(buf, sizeof(buf),
                   "手机被取走  专注已停\n%s\n中断 %d 次\n点一下接着做",
                   sess->stages[sess->current_stage].title,
                   sess->interrupt_count);
        }
      else
        {
          snprintf(buf, sizeof(buf),
                   "手机被取走  专注已停\n中断 %d 次\n点一下接着做",
                   sess->interrupt_count);
        }
      lv_label_set_text(s_info_label, buf);
      break;

    case FM_RECOVERING:
      /* Active scene: the phone came back, proactively offer to resume. */
      lv_obj_clear_flag(s_bar, LV_OBJ_FLAG_HIDDEN);
      lv_bar_set_value(s_bar, pct, LV_ANIM_OFF);
      {
        char tbuf[16];

        snprintf(tbuf, sizeof(tbuf), "%02d:%02d", remain / 60, remain % 60);
        timer_show(tbuf);
      }
      if (sess->current_stage >= 0 && sess->current_stage < sess->stage_count)
        {
          snprintf(buf, sizeof(buf),
                   "你回来了!手机已放回\n之前在做: %s\n点一下接着做",
                   sess->stages[sess->current_stage].title);
        }
      else
        {
          snprintf(buf, sizeof(buf),
                   "你回来了!手机已放回\n点一下接着做");
        }
      lv_label_set_text(s_info_label, buf);
      break;

    case FM_REVIEWING:
      lv_obj_add_flag(s_bar, LV_OBJ_FLAG_HIDDEN);
      timer_hide();
      if (sess->review_reason == FM_REVIEW_MANUAL)
        {
          lv_label_set_text(s_info_label,
                            "结束本轮?\nDONE: 选择完成任务\nCONTINUE: 继续计时");
        }
      else if (sess->review_reason == FM_REVIEW_SETTLE)
        {
          lv_label_set_text(s_info_label,
                            "结束今日专注?\n本轮是否完成任务?");
        }
      else
        {
          lv_label_set_text(s_info_label,
                            "本轮时间到\n是否完成拆解任务?\nYES: 选择任务  NO: 再开一轮");
        }
      break;

    case FM_SETTLE_CONFIRM:
      lv_obj_add_flag(s_bar, LV_OBJ_FLAG_HIDDEN);
      timer_hide();
      lv_label_set_text(s_info_label,
                        "结束今日专注?\n结算 / 继续专注");
      break;

    case FM_COMPLETED:
      lv_obj_add_flag(s_bar, LV_OBJ_FLAG_HIDDEN);
      timer_hide();
      snprintf(buf, sizeof(buf),
               "完成 %d/%d\n专注 %d 分钟 | 中断 %d 次",
               sess->completed_task_count, sess->stage_count,
               sess->elapsed_seconds / 60, sess->interrupt_count);
      lv_label_set_text(s_info_label, buf);
      break;

    default:
      break;
    }

  /* On-screen controls mirror what the CLI accepts in this state.  The
   * INTERRUPTED / RECOVERING rows are the product's whole point: the device
   * noticed on its own and is now offering to pick the session back up with
   * a single tap. */

  switch (sess->state)
    {
    case FM_READY:
      btn_config(s_btn_primary, s_btn_primary_lbl, 1,
                 "START", 0x30a030, FM_UI_CMD_START);
      btn_config(s_btn_stop, s_btn_stop_lbl, 0, NULL, 0, FM_UI_CMD_NONE);
      break;

    case FM_DURATION_SELECT:
      btn_config(s_btn_primary, s_btn_primary_lbl, 0, NULL, 0,
                 FM_UI_CMD_NONE);
      btn_config(s_btn_stop, s_btn_stop_lbl, 0, NULL, 0, FM_UI_CMD_NONE);
      break;

    case FM_FOCUSING:
      btn_config(s_btn_primary, s_btn_primary_lbl, 1,
                 "END", 0x2070d0, FM_UI_CMD_END_ROUND);
      btn_config(s_btn_stop, s_btn_stop_lbl, 1,
                 "PAUSE", 0xd08020, FM_UI_CMD_PAUSE);
      break;

    case FM_PAUSED:
    case FM_INTERRUPTED:
    case FM_RECOVERING:
      btn_config(s_btn_primary, s_btn_primary_lbl, 1,
                 "RESUME", 0x20a060, FM_UI_CMD_RESUME);
      btn_config(s_btn_stop, s_btn_stop_lbl, 1,
                 "END", 0x555c64, FM_UI_CMD_END_ROUND);
      break;

    case FM_REVIEWING:
      if (s_review_mode)
        {
          btn_config(s_btn_primary, s_btn_primary_lbl, 0, NULL, 0,
                     FM_UI_CMD_NONE);
          btn_config(s_btn_stop, s_btn_stop_lbl, 0, NULL, 0,
                     FM_UI_CMD_NONE);
        }
      else if (sess->review_reason == FM_REVIEW_MANUAL)
        {
          btn_config(s_btn_primary, s_btn_primary_lbl, 1,
                     "DONE", 0x30a030, FM_UI_CMD_REVIEW_YES);
          btn_config(s_btn_stop, s_btn_stop_lbl, 1,
                     "CONTINUE", 0x555c64, FM_UI_CMD_ROUND_CONTINUE);
        }
      else
        {
          btn_config(s_btn_primary, s_btn_primary_lbl, 1,
                     "YES", 0x30a030, FM_UI_CMD_REVIEW_YES);
          btn_config(s_btn_stop, s_btn_stop_lbl, 1,
                     "NO", 0x555c64, FM_UI_CMD_REVIEW_NONE);
        }
      break;

    case FM_SETTLE_CONFIRM:
      btn_config(s_btn_primary, s_btn_primary_lbl, 1,
                 "SETTLE", 0xd02020, FM_UI_CMD_SETTLE_CONFIRM);
      btn_config(s_btn_stop, s_btn_stop_lbl, 1,
                 "CONTINUE", 0x30a030, FM_UI_CMD_SETTLE_CANCEL);
      break;

    case FM_COMPLETED:
      btn_config(s_btn_primary, s_btn_primary_lbl, 1,
                 "NEW", 0x30a030, FM_UI_CMD_QUICKSTART);
      btn_config(s_btn_stop, s_btn_stop_lbl, 0, NULL, 0, FM_UI_CMD_NONE);
      break;

    case FM_IDLE:
      btn_config(s_btn_primary, s_btn_primary_lbl, 1,
                 "START", 0x30a030, FM_UI_CMD_QUICKSTART);
      btn_config(s_btn_stop, s_btn_stop_lbl, 0, NULL, 0, FM_UI_CMD_NONE);
      break;

    default:
      /* PLANNING: the plan is on its way, nothing to tap yet. */
      btn_config(s_btn_primary, s_btn_primary_lbl, 0, NULL, 0, FM_UI_CMD_NONE);
      btn_config(s_btn_stop, s_btn_stop_lbl, 0, NULL, 0, FM_UI_CMD_NONE);
      break;
    }

  duration_apply(sess);
  review_apply(sess);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int focus_ui_init(void)
{
  pthread_attr_t attr;
  lv_nuttx_dsc_t dsc;
  lv_nuttx_result_t res;
  const char *fb_path = focus_ui_fbdev_path();

  pthread_mutex_lock(&s_lock);

  if (!s_ready)
    {
      /* NuttX runs everything in one flat address space, so a previous run of
       * this app leaves LVGL's globals behind - lv_initialized among them.
       * lv_is_initialized() therefore reports "yes" after a restart even
       * though no display is registered, and trusting it made ui_build() run
       * against a NULL screen and trip the style assert.  What actually
       * matters is having a display, so decide on that instead. */
      if (!lv_is_initialized())
        {
          lv_init();
        }

      if (lv_display_get_default() == NULL)
        {
          memset(&res, 0, sizeof(res));

          lv_nuttx_dsc_init(&dsc);
          dsc.fb_path = fb_path;
          dsc.input_path = CONFIG_FOCUSMATE_INPUT_DEVPATH;

          if (strcmp(fb_path, CONFIG_FOCUSMATE_LCD_DEVPATH) != 0)
            {
              syslog(LOG_INFO, "[focus_ui] %s missing; using %s\n",
                     CONFIG_FOCUSMATE_LCD_DEVPATH, fb_path);
            }

          lv_nuttx_init(&dsc, &res);

          if (res.disp == NULL)
            {
              syslog(LOG_ERR,
                     "[focus_ui] display init failed on %s\n",
                     fb_path);
              pthread_mutex_unlock(&s_lock);
              return -ENODEV;
            }

          s_touch_ok = (res.indev != NULL);
          syslog(LOG_INFO, "[focus_ui] %s ready, touch %s\n",
                 fb_path,
                 s_touch_ok ? "ready" : "absent (read-only UI)");
        }
      else
        {
          syslog(LOG_INFO, "[focus_ui] reusing the display from a previous run\n");
        }

      if (lv_scr_act() == NULL)
        {
          syslog(LOG_ERR, "[focus_ui] no screen available; UI skipped\n");
          pthread_mutex_unlock(&s_lock);
          return -ENODEV;
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

  /* Board key: the panel has no touch layer, so this is the physical way in. */
  if (!s_btn_thread_run)
    {
      s_btn_thread_run = 1;
      if (pthread_create(&s_btn_thread, NULL, button_thread, NULL) != 0)
        {
          s_btn_thread_run = 0;
          syslog(LOG_ERR, "[focus_ui] key thread create failed\n");
        }
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

void focus_ui_enter_review_selection(const fm_session_t *sess)
{
  pthread_mutex_lock(&s_lock);
  s_review_mode = 1;
  s_review_selection = -1;
  if (s_ready && sess != NULL)
    {
      ui_apply(sess);
    }
  pthread_mutex_unlock(&s_lock);
}

int focus_ui_take_review_selection(void)
{
  int result;

  pthread_mutex_lock(&s_cmd_lock);
  result = s_review_selection >= 0 ? s_review_selection + 1 : 0;
  s_review_selection = -1;
  pthread_mutex_unlock(&s_cmd_lock);
  return result;
}

int focus_ui_take_duration(void)
{
  int result;

  pthread_mutex_lock(&s_cmd_lock);
  result = s_duration_selected;
  s_duration_selected = 0;
  pthread_mutex_unlock(&s_cmd_lock);
  return result;
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
  if (s_btn_thread_run)
    {
      s_btn_thread_run = 0;
      pthread_join(s_btn_thread, NULL);
    }

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

fm_ui_cmd_t focus_ui_take_command(void)
{
  fm_ui_cmd_t cmd;

  pthread_mutex_lock(&s_cmd_lock);
  cmd = s_pending_cmd;
  s_pending_cmd = FM_UI_CMD_NONE;
  pthread_mutex_unlock(&s_cmd_lock);

  return cmd;
}

int focus_ui_has_touch(void)
{
  return s_ready && s_touch_ok;
}
