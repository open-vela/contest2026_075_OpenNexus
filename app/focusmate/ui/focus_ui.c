/****************************************************************************
 * app/focusmate/ui/focus_ui.c
 *
 * FocusMate LVGL UI skeleton (M3).
 *
 * Three simple label-based pages driven by fm_state:
 *   - IDLE/PLANNING/READY  : product name + current state
 *   - FOCUSING/PAUSED/...  : current stage + state
 *   - COMPLETED            : summary line
 * M5 replaces the labels with a real countdown + progress bar.
 ****************************************************************************/

#include "focus_ui.h"

#include <stdio.h>

#include <lvgl/lvgl.h>

static lv_obj_t *s_screen;
static lv_obj_t *s_title_label;
static lv_obj_t *s_state_label;
static lv_obj_t *s_info_label;

static const lv_color_t s_state_colors[8] = {
  LV_COLOR_MAKE(0x80, 0x80, 0x80), /* IDLE        grey  */
  LV_COLOR_MAKE(0xf0, 0xa0, 0x00), /* PLANNING    amber */
  LV_COLOR_MAKE(0x30, 0xa0, 0x30), /* READY       green */
  LV_COLOR_MAKE(0x20, 0x70, 0xd0), /* FOCUSING    blue  */
  LV_COLOR_MAKE(0xd0, 0x80, 0x20), /* PAUSED      orange*/
  LV_COLOR_MAKE(0xd0, 0x20, 0x20), /* INTERRUPTED red   */
  LV_COLOR_MAKE(0x40, 0x90, 0xd0), /* RECOVERING  sky  */
  LV_COLOR_MAKE(0x20, 0xa0, 0x60)  /* COMPLETED   teal */
};

static void ui_set_labels(const fm_session_t *sess)
{
  char buf[256];
  const char *state_name = fm_state_name(sess->state);

  lv_label_set_text(s_title_label, "FocusMate");
  lv_obj_set_style_text_color(s_state_label,
                              s_state_colors[sess->state], 0);
  lv_label_set_text(s_state_label, state_name);

  switch (sess->state) {
  case FM_IDLE:
    lv_label_set_text(s_info_label,
                      "输入目标与可用时间，\n开始一次专注");
    break;
  case FM_PLANNING:
    lv_label_set_text(s_info_label, "AI 正在拆解计划...");
    break;
  case FM_READY:
    snprintf(buf, sizeof(buf), "%s\n共 %d 分钟 / %d 个阶段\n按 Start 开始",
             sess->goal[0] ? sess->goal : "(无目标)",
             sess->total_minutes, sess->stage_count);
    lv_label_set_text(s_info_label, buf);
    break;
  case FM_FOCUSING:
  case FM_PAUSED:
  case FM_INTERRUPTED:
  case FM_RECOVERING:
    if (sess->current_stage >= 0 && sess->current_stage < sess->stage_count) {
      snprintf(buf, sizeof(buf),
               "阶段 %d/%d\n%s\n已专注 %d 秒\n中断 %d 次",
               sess->current_stage + 1, sess->stage_count,
               sess->stages[sess->current_stage].title,
               sess->elapsed_seconds, sess->interrupt_count);
    } else {
      snprintf(buf, sizeof(buf), "已专注 %d 秒\n中断 %d 次",
               sess->elapsed_seconds, sess->interrupt_count);
    }
    lv_label_set_text(s_info_label, buf);
    break;
  case FM_COMPLETED:
    snprintf(buf, sizeof(buf), "全部完成！\n专注 %d 秒 / 中断 %d 次",
             sess->elapsed_seconds, sess->interrupt_count);
    lv_label_set_text(s_info_label, buf);
    break;
  default:
    break;
  }
}

int focus_ui_init(void)
{
  s_screen = lv_scr_act();

  s_title_label = lv_label_create(s_screen);
  lv_obj_set_style_text_font(s_title_label, &lv_font_montserrat_32, 0);
  lv_obj_align(s_title_label, LV_ALIGN_TOP_MID, 0, 30);

  s_state_label = lv_label_create(s_screen);
  lv_obj_set_style_text_font(s_state_label, &lv_font_montserrat_20, 0);
  lv_obj_align(s_state_label, LV_ALIGN_CENTER, 0, -40);

  s_info_label = lv_label_create(s_screen);
  lv_obj_set_style_text_align(s_info_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(s_info_label, LV_ALIGN_CENTER, 0, 30);

  return 0;
}

void focus_ui_refresh(const fm_session_t *sess)
{
  ui_set_labels(sess);
}

void focus_ui_notice(const char *msg)
{
  lv_label_set_text(s_info_label, msg);
}
