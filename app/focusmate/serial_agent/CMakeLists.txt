# Opt-in bring-up profile: upstream ai_agent core, USB serial HTTP transport.
set(FM_AGENT ${NUTTX_APPS_DIR}/../packages/ai_agent)
set(FM_AGENT_SRCS)
foreach(src core/message_bus.c core/agent_loop.c core/agent_trace.c
 core/context_builder.c core/memory_store.c core/session_mgr.c core/arena_alloc.c
 llm/llm_proxy.c llm/llm_parse.c llm/llm_router.c llm/llm_cache.c
 infra/config_store.c tools/skill_loader.c tools/tool_guard.c)
 list(APPEND FM_AGENT_SRCS ${FM_AGENT}/src/${src})
endforeach()
nuttx_add_application(NAME serial_agent STACKSIZE 16384 PRIORITY 100
 SRCS serial_agent.c serial_transport.c minimal_tools.c ${FM_AGENT_SRCS}
 INCLUDE_DIRECTORIES ${FM_AGENT}/src ${FM_AGENT}/include
 ${NUTTX_APPS_DIR}/netutils/cjson/cJSON)
target_compile_definitions(apps_serial_agent PRIVATE
 CONFIG_EXAMPLES_AI_AGENT_VELA_DATA_DIR="/data/serial_agent")
