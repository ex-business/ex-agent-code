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
qt_add_resources(ex_agent_chat "terminal_hibernation" PREFIX "/terminal-hibernation"
    BASE "plugins/session_history" FILES plugins/session_history/pi_activity.mjs plugins/session_history/pi_hibernation.mjs)
qt_add_resources(ex_agent_chat "appearance_packs" PREFIX "/appearance" BASE plugins/appearance/packs
    FILES plugins/appearance/packs/dark.json plugins/appearance/packs/en.json plugins/appearance/packs/light.json plugins/appearance/packs/zh-CN.json)
qt_add_qml_module(ex_agent_chat
    URI ExAgent.Chat
    VERSION 1.0
    DEPENDENCIES QtWebEngine
    SOURCES
        plugins/diff_review/session_review.cpp
        plugins/task_links/task_provider.cpp plugins/task_links/task_provider.h
        plugins/task_links/task_status.cpp plugins/task_links/task_status.h plugins/task_links/workspace_task_links.cpp
        plugins/git_changes/git_changes.h plugins/git_changes/git_changes.cpp
        plugins/git_changes/git_actions.h plugins/git_changes/git_actions.cpp
        plugins/git_changes/untracked_preview.cpp plugins/git_changes/git_read.cpp plugins/git_changes/revision_range.cpp
        plugins/workspace_viewers/workspace_preview.h plugins/workspace_viewers/workspace_preview.cpp
        plugins/workspace_viewers/viewer_registry.h plugins/workspace_viewers/viewer_registry.cpp
        plugins/workspace_viewers/viewer_data.h plugins/workspace_viewers/viewer_data.cpp
        plugins/workspace_viewers/html/plugin.cpp plugins/workspace_viewers/html/safe_web_profile.h plugins/workspace_viewers/html/safe_web_profile.cpp
        plugins/workspace_viewers/mermaid/plugin.cpp plugins/workspace_viewers/pdf/plugin.cpp
        plugins/workspace_viewers/table/plugin.cpp plugins/workspace_viewers/notebook/plugin.cpp
        plugins/workspace_editor/editor_document.h plugins/workspace_editor/editor_document.cpp
        plugins/terminal_settings/terminal_preferences.h plugins/terminal_settings/terminal_preferences.cpp
        plugins/appearance/appearance_registry.h
        plugins/interactive_terminal/terminal_surface.h plugins/interactive_terminal/terminal_surface.cpp
        plugins/session_history/session_console.h plugins/session_history/session_console.cpp
        plugins/session_history/terminal_hibernation.cpp plugins/session_history/history_provider.cpp plugins/session_history/terminal_backend.cpp
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
        presentation/code/script_preview.h presentation/code/script_preview.cpp
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
target_sources(ex_agent_chat PRIVATE presentation/code/code_color_span.cpp)
option(EX_AGENT_GO_AST "使用 Go 语法树和 VS Code 主题渲染代码" OFF)
if(EX_AGENT_GO_AST)
    enable_language(C)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(TreeSitter REQUIRED IMPORTED_TARGET tree-sitter>=0.22)
    include(FetchContent)
    FetchContent_Declare(ex_go_grammar
        URL https://codeload.github.com/tree-sitter/tree-sitter-go/tar.gz/refs/tags/v0.23.4
        URL_HASH SHA256=967870d7d120e9b760e538aeb8331a72f70ffcca4f1eaf1e1dea5375886d25d2
        SOURCE_SUBDIR unused)
    FetchContent_MakeAvailable(ex_go_grammar)
    add_library(ex_go_parser STATIC "${ex_go_grammar_SOURCE_DIR}/src/parser.c")
    target_sources(ex_agent_chat PRIVATE presentation/code/go_ast_highlighter.cpp presentation/code/vscode_token_theme.cpp)
    target_link_libraries(ex_agent_chat PRIVATE PkgConfig::TreeSitter ex_go_parser)
    target_compile_definitions(ex_agent_chat PRIVATE EX_AGENT_GO_AST)
    qt_add_resources(ex_agent_chat code_themes PREFIX "/code-themes" BASE presentation/code/themes
        FILES presentation/code/themes/light.json presentation/code/themes/dark.json)
endif()
option(EX_AGENT_KDE_HIGHLIGHTING "使用 KDE 语法高亮渲染 Go、C 和 Bash 代码块" OFF)
if(EX_AGENT_KDE_HIGHLIGHTING)
    find_package(KF6SyntaxHighlighting CONFIG REQUIRED)
    target_sources(ex_agent_chat PRIVATE presentation/code/kde_code_highlighter.cpp)
    target_link_libraries(ex_agent_chat PUBLIC KF6::SyntaxHighlighting)
    target_compile_definitions(ex_agent_chat PUBLIC EX_AGENT_KDE_HIGHLIGHTING)
endif()
find_package(yaml-cpp CONFIG REQUIRED)
target_link_libraries(ex_agent_chat PRIVATE yaml-cpp::yaml-cpp)
target_link_libraries(ex_agent_chat PUBLIC ex_agent_domain ex_agent_ipc ex_agent_codex Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Concurrent Qt6::WebEngineQuick Qt6::Pdf)
target_include_directories(ex_agent_chat PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/plugins/git_changes" "${CMAKE_CURRENT_SOURCE_DIR}/plugins/workspace_viewers" "${CMAKE_CURRENT_SOURCE_DIR}/plugins/workspace_editor" "${CMAKE_CURRENT_SOURCE_DIR}/plugins/terminal_settings" "${CMAKE_CURRENT_SOURCE_DIR}/plugins/appearance" "${CMAKE_CURRENT_SOURCE_DIR}/plugins/interactive_terminal" "${CMAKE_CURRENT_SOURCE_DIR}/plugins/session_history" "${CMAKE_CURRENT_SOURCE_DIR}/application/agent" "${CMAKE_CURRENT_SOURCE_DIR}/application/chat" "${CMAKE_CURRENT_SOURCE_DIR}/presentation/chat" "${CMAKE_CURRENT_SOURCE_DIR}/presentation/code")
if(APPLE)
    find_path(VTERM_INCLUDE_DIR vterm.h REQUIRED)
    find_file(VTERM_ARCHIVE libvterm.a PATH_SUFFIXES lib REQUIRED)
    target_include_directories(ex_agent_chat PRIVATE "${VTERM_INCLUDE_DIR}")
    target_link_libraries(ex_agent_chat PRIVATE "${VTERM_ARCHIVE}")
endif()
target_compile_options(ex_agent_chat PRIVATE -Wall -Wextra -Wpedantic)
target_include_directories(ex_agent_chat PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/plugins/task_links" "${CMAKE_CURRENT_SOURCE_DIR}/presentation/text")
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

qt_add_resources(ex_agent_chat viewer_assets PREFIX "/workspace-viewers" BASE plugins/workspace_viewers/mermaid/assets FILES plugins/workspace_viewers/mermaid/assets/mermaid.js plugins/workspace_viewers/mermaid/assets/mermaid.js.LEGAL.txt plugins/workspace_viewers/mermaid/assets/LICENSE.txt plugins/workspace_viewers/mermaid/assets/THIRD_PARTY_NOTICES.txt plugins/workspace_viewers/mermaid/assets/provenance.json)

target_include_directories(ex_agent_chat PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/plugins/workspace_viewers/html)
