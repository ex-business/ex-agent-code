qt_add_library(ex_agent_codex STATIC
    codex/codex_account.cpp codex/codex_account.h
    codex/codex_connection.cpp codex/codex_connection.h
    codex/model_catalog.cpp codex/model_catalog.h
    codex/codex_run.cpp codex/codex_run.h
    codex/codex_worker.cpp codex/codex_worker.h)
target_link_libraries(ex_agent_codex PUBLIC ex_agent_domain Qt6::Core)
target_compile_options(ex_agent_codex PRIVATE -Wall -Wextra -Wpedantic)
