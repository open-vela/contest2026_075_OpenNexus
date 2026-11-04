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
      core/focus_flow.c
      ui/focus_pixel.c
      agent/focus_agent.c
      sensor/phone_sensor.c
      storage/focus_storage.c
      ui/focus_ui.c
      ui/lv_font_simsun_24_cjk.c
    INCLUDE_DIRECTORIES
      ${CMAKE_CURRENT_SOURCE_DIR}
      ${NUTTX_APPS_DIR}/netutils/cjson/cJSON
      ${NUTTX_APPS_DIR}/packages/ai_agent/include
      ${NUTTX_APPS_DIR}/packages/ai_agent/src
    STACKSIZE 40960
    PRIORITY 100
  )
endif()

# Diagnostic profile; does not assert full CONFIG_EXAMPLES_AI_AGENT_VELA support.
if(FOCUSMATE_SERIAL_AGENT)
  add_subdirectory(serial_agent)
endif()

if(FOCUSMATE_PHONE_GPIO OR CONFIG_FOCUSMATE_PHONE_GPIO)
  if(NOT CONFIG_ARCH_CHIP_SF32LB52)
    message(FATAL_ERROR "FocusMate PA41 phone GPIO is SF32LB52-specific")
  endif()
  target_sources(apps_focusmate PRIVATE sensor/phone_gpio.c)
  target_compile_definitions(apps_focusmate PRIVATE CONFIG_FOCUSMATE_PHONE_GPIO=1)
endif()

if(FOCUSMATE_BOARD_AUDIO)
  if(NOT CONFIG_AUDIO OR NOT CONFIG_ARCH_CHIP_SF32LB52)
    message(FATAL_ERROR "FocusMate board audio requires SF32LB52 and CONFIG_AUDIO=y")
  endif()
  target_sources(apps_focusmate PRIVATE audio/focus_audio.c)
  target_compile_definitions(apps_focusmate PRIVATE CONFIG_FOCUSMATE_BOARD_AUDIO=1)
  nuttx_add_application(NAME audio_probe STACKSIZE 8192 PRIORITY 100
    SRCS diagnostics/audio_probe.c INCLUDE_DIRECTORIES ${CMAKE_CURRENT_SOURCE_DIR}/audio)
  nuttx_add_application(NAME focusmate_ctl STACKSIZE 4096 PRIORITY 100
    SRCS diagnostics/focusmate_ctl.c)
endif()

# Optional real-board BLE diagnostic/relay, independent of UI mock networking.
if(FOCUSMATE_BLE_RELAY)
  if(NOT CONFIG_UART_BTH4 OR NOT CONFIG_BT_ORIGINAL_API OR NOT CONFIG_ARCH_CHIP_SF32LB52)
    message(FATAL_ERROR "FocusMate BLE requires SF32LB52 UART_BTH4 and the original Zblue API")
  endif()
  nuttx_add_application(NAME focusmate_ble STACKSIZE 16384 PRIORITY 100
    SRCS network/focus_ble.c network/ble_frame.c network/hci_trace.c diagnostics/ble_hci_probe.c
    INCLUDE_DIRECTORIES ${CMAKE_CURRENT_SOURCE_DIR}/network)
  target_sources(apps_focusmate_ble PRIVATE network/h4_nuttx.c)
  target_link_libraries(apps_focusmate_ble PRIVATE zblue_headers)
endif()

# Step-1 isolated instance of the genuine ai_agent cron scheduler. No LLM.
if(FOCUSMATE_REMINDER)
  set(FM_AGENT ${NUTTX_APPS_DIR}/packages/ai_agent)
  nuttx_add_application(NAME focusmate_remind STACKSIZE 32768 PRIORITY 100
    SRCS reminder/reminder.c reminder/cron_instance.c
    INCLUDE_DIRECTORIES ${CMAKE_CURRENT_SOURCE_DIR}/reminder
      ${FM_AGENT}/include ${FM_AGENT}/src
      ${NUTTX_APPS_DIR}/netutils/cjson/cJSON)
  target_sources(apps_focusmate PRIVATE reminder/prompt_state.c)
  target_compile_definitions(apps_focusmate PRIVATE CONFIG_FOCUSMATE_REMINDER=1)
endif()
