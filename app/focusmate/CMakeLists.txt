# Contest 2026 team 075 - FocusMate
#
# FocusMate (AI 桌面专注伙伴) - M3 application skeleton.

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
    STACKSIZE 8192
    PRIORITY 100
  )
endif()
