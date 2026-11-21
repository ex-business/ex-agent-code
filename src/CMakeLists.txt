add_library(ex_agent_domain INTERFACE)
target_include_directories(ex_agent_domain INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(ex_agent_domain INTERFACE Qt6::Core)

qt_add_library(ex_agent_chat STATIC)
qt_add_qml_module(ex_agent_chat
    URI ExAgent.Chat
    VERSION 1.0
    SOURCES
        application/chat/chat_controller.h application/chat/chat_controller.cpp
        infrastructure/storage/session_repository.h infrastructure/storage/session_repository.cpp
        infrastructure/storage/conversation_resource_repository.h infrastructure/storage/conversation_resource_repository.cpp
        presentation/chat/message_list_model.h presentation/chat/message_list_model.cpp
        presentation/chat/session_list_model.h presentation/chat/session_list_model.cpp
        presentation/code/code_line_model.h presentation/code/code_line_model.cpp
        presentation/code/syntax_highlighter.h presentation/code/syntax_highlighter.cpp
        presentation/text/text_selection_rules.h presentation/text/text_selection_rules.cpp
        presentation/text/markdown_formatter.h presentation/text/markdown_formatter.cpp
        presentation/text/selection_scroll_clock.h presentation/text/selection_scroll_clock.cpp
        presentation/text/chat_selection_tracker.h presentation/text/chat_selection_tracker.cpp
        presentation/text/chat_selection_document.h presentation/text/chat_selection_document.cpp
        presentation/text/selection_coordinator.h presentation/text/selection_coordinator.cpp
        presentation/workspace/workspace_controller.h presentation/workspace/workspace_controller.cpp
)
target_link_libraries(ex_agent_chat PUBLIC ex_agent_domain Qt6::Gui Qt6::Qml Qt6::Quick)
target_include_directories(ex_agent_chat PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/application/chat" "${CMAKE_CURRENT_SOURCE_DIR}/presentation/chat" "${CMAKE_CURRENT_SOURCE_DIR}/presentation/code")
target_compile_options(ex_agent_chat PRIVATE -Wall -Wextra -Wpedantic)
target_include_directories(ex_agent_chat PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/presentation/text")
target_include_directories(ex_agent_chat PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/presentation/workspace")

qt_add_executable(ex-agent-code MACOSX_BUNDLE app/main.cpp app/scroll_diagnostics.h app/scroll_diagnostics.cpp app/pixel_scroll_filter.h app/pixel_scroll_filter.cpp)
target_link_libraries(ex-agent-code PRIVATE ex_agent_chat ex_agent_chatplugin Qt6::Quick Qt6::QuickControls2)
if(APPLE)
    target_sources(ex_agent_chat PRIVATE platform/macos/word_boundaries.cpp)
    target_link_libraries(ex_agent_chat PRIVATE "-framework CoreFoundation")
    target_sources(ex-agent-code PRIVATE platform/macos/window_chrome.mm platform/macos/inactive_hover.mm)
    target_link_libraries(ex-agent-code PRIVATE "-framework AppKit")
endif()
set_target_properties(ex-agent-code PROPERTIES
    MACOSX_BUNDLE_BUNDLE_NAME "EX Agent Code"
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.exbusiness.agentcode"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
)
install(TARGETS ex-agent-code BUNDLE DESTINATION . RUNTIME DESTINATION bin)
