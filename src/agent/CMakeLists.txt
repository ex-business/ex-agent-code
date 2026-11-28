# 运行契约与注册表独立于各引擎；增加适配器不需要让Codex依赖其实现。
qt_add_library(ex_agent_runtime_contract STATIC runtime_run.h)
target_link_libraries(ex_agent_runtime_contract PUBLIC ex_agent_domain Qt6::Core)
qt_add_library(ex_agent_openai STATIC openai/credentials.cpp openai/credentials.h
    openai/file_tools.cpp openai/file_tools.h openai/responses_run.cpp openai/responses_run.h)
target_link_libraries(ex_agent_openai PUBLIC ex_agent_runtime_contract Qt6::Network)
target_compile_options(ex_agent_openai PRIVATE -Wall -Wextra -Wpedantic)
qt_add_library(ex_agent_codex STATIC
    ../plugins/diff_review/patch_lines.cpp ../plugins/diff_review/codex_evidence.cpp
    codex/thread_input.cpp codex/thread_input.h
    ../plugins/agent_activity/activity_store.cpp
    codex/codex_account.cpp codex/codex_account.h
    codex/codex_connection.cpp codex/codex_connection.h
    codex/model_catalog.cpp codex/model_catalog.h
    codex/codex_run.cpp codex/codex_run.h)
target_link_libraries(ex_agent_codex PUBLIC ex_agent_runtime_contract)
target_compile_options(ex_agent_codex PRIVATE -Wall -Wextra -Wpedantic)
qt_add_library(ex_agent_cli STATIC cli/pi_guard_install.cpp cli/process_scope.cpp cli/process_scope.h cli/cli_run.cpp cli/cli_run.h cli/session_binding.cpp cli/session_binding.h cli/pi_run.cpp cli/claude_run.cpp)
qt_add_resources(ex_agent_cli native_cli_guard PREFIX "/native-cli" BASE cli FILES cli/pi_guard.mjs)
target_link_libraries(ex_agent_cli PUBLIC ex_agent_runtime_contract ex_agent_codex)
target_compile_options(ex_agent_cli PRIVATE -Wall -Wextra -Wpedantic)
qt_add_library(ex_agent_runtime STATIC runtime_worker.cpp runtime_worker.h)
target_link_libraries(ex_agent_runtime PUBLIC ex_agent_codex ex_agent_openai ex_agent_cli)
target_compile_options(ex_agent_runtime PRIVATE -Wall -Wextra -Wpedantic)
