/* SPDX-License-Identifier: Apache-2.0
 * Compile the workspace's genuine ai_agent scheduler as an isolated instance.
 * No LLM, fake Agent loop, or alternate timer implementation is involved.
 * Namespacing and a separate store prevent collisions with the full Agent.
 */
#include "agent_config.h"
#undef AGENT_CRON_FILE
#define AGENT_CRON_FILE "/data/focusmate/reminder-cron.json"
#undef AGENT_CRON_CHECK_INTERVAL_MS
#define AGENT_CRON_CHECK_INTERVAL_MS 1000
#define cron_service_init fm_cron_init
#define cron_service_start fm_cron_start
#define cron_service_stop fm_cron_stop
#define cron_add_job fm_cron_add
#define cron_remove_job fm_cron_remove
#define cron_list_jobs fm_cron_list
#define tool_registry_execute fm_reminder_execute
#define message_bus_push_outbound fm_reminder_outbound
#include "infra/cron_service.c"
