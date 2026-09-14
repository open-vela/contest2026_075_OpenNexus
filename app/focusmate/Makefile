############################################################################
# Contest 2026 team 075 - FocusMate (Make build support)
############################################################################

include $(APPDIR)/Make.defs

PROGNAME  = focusmate
PRIORITY  = 100
STACKSIZE = 40960
MODULE    = $(CONFIG_LVX_USE_DEMO_FOCUSMATE)

# Keep the Make build's include set aligned with CMakeLists.txt.
CFLAGS += ${INCDIR_PREFIX}$(CURDIR)
CFLAGS += ${INCDIR_PREFIX}$(APPDIR)/packages/ai_agent/include
CFLAGS += ${INCDIR_PREFIX}$(APPDIR)/packages/ai_agent/src

CSRCS += core/focus_state.c core/focus_timer.c
CSRCS += agent/focus_agent.c
CSRCS += sensor/phone_sensor.c
CSRCS += storage/focus_storage.c
CSRCS += ui/focus_ui.c
CSRCS += ui/lv_font_simsun_24_cjk.c
MAINSRC = focusmate_main.c

include $(APPDIR)/Application.mk
