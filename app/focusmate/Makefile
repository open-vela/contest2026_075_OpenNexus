############################################################################
# Contest 2026 team 075 - FocusMate (Make build support)
############################################################################

include $(APPDIR)/Make.defs

PROGNAME  = focusmate
PRIORITY  = 100
STACKSIZE = 8192
MODULE    = $(CONFIG_LVX_USE_DEMO_FOCUSMATE)

CSRCS += core/focus_state.c core/focus_timer.c
CSRCS += agent/focus_agent.c
CSRCS += sensor/phone_sensor.c
CSRCS += storage/focus_storage.c
CSRCS += ui/focus_ui.c
MAINSRC = focusmate_main.c

include $(APPDIR)/Application.mk
