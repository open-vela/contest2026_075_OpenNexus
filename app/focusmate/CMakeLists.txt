# Contest 2026 team 075 - FocusMate
#
# FocusMate (AI 桌面专注伙伴) - M4: focus-planner skill integration.

# FocusMate's UI shows Chinese text, which needs LVGL's built-in SimSun CJK
# font.  The Kconfig switch CONFIG_LV_FONT_SIMSUN_16_CJK does not survive the
# defconfig -> .config path on this board configuration, so LVGL's
# lv_conf_internal.h falls through to its "#define LV_FONT_SIMSUN_16_CJK 0"
# branch: lv_font_simsun_16_cjk.c then compiles to an empty object and every
# Chinese glyph renders as the "missing glyph" placeholder box.
#
# lv_conf_internal.h guards that macro with #ifndef, so defining it directly
# on the lvgl target re-enables the font without patching the upstream port.
# cmake_language(DEFER) is used because the lvgl target is created by a
# sibling directory whose configure order relative to this file is not
# guaranteed to have happened yet.
function(focusmate_enable_cjk_font)
  if(TARGET lvgl)
    target_compile_definitions(lvgl PRIVATE LV_FONT_SIMSUN_16_CJK=1)
  endif()
endfunction()

cmake_language(DEFER CALL focusmate_enable_cjk_font)

if(CONFIG_LVX_USE_DEMO_FOCUSMATE)
  nuttx_add_application(
    NAME focusmate
    SRCS
      focusmate_main.c
      core/focus_state.c
      core/focus_timer.c
      agent/focus_agent.c
      sensor/phone_sensor.c
      storage/focus_storage.c
      ui/focus_ui.c
    INCLUDE_DIRECTORIES
      ${CMAKE_CURRENT_SOURCE_DIR}
      ${NUTTX_APPS_DIR}/netutils/cjson/cJSON
      ${NUTTX_APPS_DIR}/packages/ai_agent/include
    STACKSIZE 16384
    PRIORITY 100
  )
endif()
