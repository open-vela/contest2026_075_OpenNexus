#ifndef FOCUS_FLOW_H
#define FOCUS_FLOW_H
#include <stdbool.h>
#define FLOW_MAX_TASKS 12
#define FLOW_TITLE 160
typedef enum { P_STANDBY, P_WELCOME, P_LIST, P_ADD, P_RECORD,
 P_CONFIRM, P_PREPARE, P_WAIT, P_RUNNING, P_PAUSED, P_END,
 P_REVIEW, P_MORE, P_HELP, P_SUMMARY, P_PAUSE_MENU, P_DETAIL, P_NOTE } flow_page;
typedef struct { unsigned id; char title[FLOW_TITLE]; bool done; } flow_task;
typedef struct {
 flow_page page, before_record, before_end;
 flow_task tasks[FLOW_MAX_TASKS]; int count, selected, cursor;
 int minutes, remaining, round_seconds, total_seconds, rounds, interruptions;
 bool round_open, phone_pause, bypass, wait_resume, sound, heard;
 bool phone_raw, phone_stable, phone_known; unsigned debounce_ms;
 bool network; int battery; bool charging; bool battery_known;
 bool recovered, review_pending; char draft[FLOW_TITLE], speech[FLOW_TITLE], note[FLOW_TITLE];
 char notice[192];
} focus_flow;
void flow_init(focus_flow *f);
void flow_action(focus_flow *f, const char *action);
void flow_phone(focus_flow *f, bool present);
void flow_poll(focus_flow *f, unsigned elapsed_ms);
void flow_tick(focus_flow *f, unsigned seconds);
const char *flow_page_name(flow_page p);
const char *flow_title(const focus_flow *f);
#endif
