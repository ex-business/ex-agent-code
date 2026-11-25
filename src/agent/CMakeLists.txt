qt_add_library(ex_agent_codex STATIC
    ../plugins/diff_review/patch_lines.cpp ../plugins/diff_review/codex_evidence.cpp
    codex/thread_input.cpp codex/thread_input.h
    ../plugins/agent_activity/activity_store.cpp
    codex/codex_account.cpp codex/codex_account.h
    codex/codex_connection.cpp codex/codex_connection.h
    codex/model_catalog.cpp codex/model_catalog.h
    codex/codex_run.cpp codex/codex_run.h
    codex/codex_worker.cpp codex/codex_worker.h)
target_link_libraries(ex_agent_codex PUBLIC ex_agent_domain Qt6::Core)
target_compile_options(ex_agent_codex PRIVATE -Wall -Wextra -Wpedantic)
