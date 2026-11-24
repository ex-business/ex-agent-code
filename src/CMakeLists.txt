add_library(ex_agent_domain INTERFACE)
target_include_directories(ex_agent_domain INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(ex_agent_domain INTERFACE Qt6::Core)

add_subdirectory(agent)
add_library(ex_git_worktree STATIC plugins/git_worktree/git_worktree_provider.cpp)
target_link_libraries(ex_git_worktree PUBLIC ex_agent_domain)

# 通信底座只依赖Core/Network，后台模式不创建GUI或加载QML。
qt_add_library(ex_agent_ipc STATIC
    domain/content/content_block.h
    domain/ipc/protocol.h
    infrastructure/ipc/rpc_channel.h infrastructure/ipc/rpc_channel.cpp
    application/services/local_service_process.h application/services/local_service_process.cpp
    application/services/content_block_store.h application/services/content_block_store.cpp
    application/services/content_block_adapters.h application/services/content_block_adapters.cpp

)
target_include_directories(ex_agent_ipc PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(ex_agent_ipc PUBLIC Qt6::Core Qt6::Network)
target_compile_options(ex_agent_ipc PRIVATE -Wall -Wextra -Wpedantic)

qt_add_library(ex_agent_worker STATIC services/local/service_worker.h services/local/service_worker.cpp)
target_link_libraries(ex_agent_worker PUBLIC ex_agent_ipc ex_agent_codex)

qt_add_library(ex_agent_chat STATIC)
qt_add_qml_module(ex_agent_chat
    URI ExAgent.Chat
    VERSION 1.0
    SOURCES
        domain/project/project_catalog.h domain/project/project_catalog.cpp
        application/project/project_navigator.h application/project/project_navigator.cpp
        infrastructure/storage/conversation_image.h infrastructure/storage/conversation_image.cpp
        infrastructure/storage/history_migration.h infrastructure/storage/history_migration.cpp
        infrastructure/storage/project_repository.h infrastructure/storage/project_repository.cpp
        application/agent/history_context.h application/agent/history_context.cpp
        application/agent/agent_controller.h application/agent/agent_controller.cpp
        application/chat/conversation_transfer.h application/chat/conversation_transfer.cpp
        application/chat/chat_controller.h application/chat/chat_controller.cpp
        application/services/internal_content_task.h application/services/internal_content_task.cpp
        application/services/media_file_exporter.h application/services/media_file_exporter.cpp
        application/services/media_catalog.h application/services/media_catalog.cpp
        application/services/media_content_task.h application/services/media_content_task.cpp
        infrastructure/storage/session_repository.h infrastructure/storage/session_repository.cpp
        infrastructure/storage/conversation_resource_repository.h infrastructure/storage/conversation_resource_repository.cpp
        presentation/blocks/block_window_model.h
        presentation/blocks/block_view_registry.h presentation/blocks/block_view_registry.cpp
        presentation/chat/activity_group_projection.h presentation/chat/activity_group_projection.cpp
        presentation/chat/message_list_model.h presentation/chat/message_list_model.cpp
        presentation/chat/session_list_model.h presentation/chat/session_list_model.cpp
        presentation/code/code_line_model.h presentation/code/code_line_model.cpp
        presentation/code/syntax_highlighter.h presentation/code/syntax_highlighter.cpp
        presentation/text/text_selection_rules.h presentation/text/text_selection_rules.cpp
        presentation/text/rich_text_preparation.h presentation/text/rich_text_preparation.cpp
        presentation/text/markdown_formatter.h presentation/text/markdown_formatter.cpp
        presentation/text/selection_scroll_clock.h presentation/text/selection_scroll_clock.cpp
        presentation/text/chat_selection_tracker.h presentation/text/chat_selection_tracker.cpp
        presentation/text/chat_selection_document.h presentation/text/chat_selection_document.cpp
        presentation/text/selection_coordinator.h presentation/text/selection_coordinator.cpp
        presentation/workspace/workspace_controller.h presentation/workspace/workspace_controller.cpp
)
target_link_libraries(ex_agent_chat PUBLIC ex_agent_domain ex_agent_ipc ex_agent_codex Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Concurrent)
target_include_directories(ex_agent_chat PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/application/agent" "${CMAKE_CURRENT_SOURCE_DIR}/application/chat" "${CMAKE_CURRENT_SOURCE_DIR}/presentation/chat" "${CMAKE_CURRENT_SOURCE_DIR}/presentation/code")
target_compile_options(ex_agent_chat PRIVATE -Wall -Wextra -Wpedantic)
target_include_directories(ex_agent_chat PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/presentation/text")
target_include_directories(ex_agent_chat PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/presentation/workspace")

qt_add_executable(ex-agent-code MACOSX_BUNDLE app/main.cpp app/profiled_application.h app/disclosure_input_filter.h app/disclosure_input_filter.cpp app/scroll_diagnostics.h app/scroll_diagnostics.cpp app/pixel_scroll_filter.h app/pixel_scroll_filter.cpp)
target_link_libraries(ex-agent-code PRIVATE ex_git_worktree ex_agent_worker ex_agent_chat ex_agent_chatplugin Qt6::Quick Qt6::QuickControls2)
if(APPLE)
    target_sources(ex_agent_chat PRIVATE platform/macos/word_boundaries.cpp)
    target_link_libraries(ex_agent_chat PRIVATE "-framework CoreFoundation")
    target_sources(ex-agent-code PRIVATE platform/macos/window_chrome.mm platform/macos/inactive_hover.mm platform/macos/drawable_timing.mm)
    target_link_libraries(ex-agent-code PRIVATE "-framework AppKit" "-framework QuartzCore")
endif()
set_target_properties(ex-agent-code PROPERTIES
    MACOSX_BUNDLE_BUNDLE_NAME "EX Agent Code"
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.exbusiness.agentcode"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
)
install(TARGETS ex-agent-code BUNDLE DESTINATION . RUNTIME DESTINATION bin)
