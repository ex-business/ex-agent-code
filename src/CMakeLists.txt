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
)
target_link_libraries(ex_agent_chat PUBLIC ex_agent_domain Qt6::Gui Qt6::Qml)
target_include_directories(ex_agent_chat PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/application/chat" "${CMAKE_CURRENT_SOURCE_DIR}/presentation/chat" "${CMAKE_CURRENT_SOURCE_DIR}/presentation/code")
target_compile_options(ex_agent_chat PRIVATE -Wall -Wextra -Wpedantic)

qt_add_executable(ex-agent-code MACOSX_BUNDLE app/main.cpp app/scroll_diagnostics.h app/scroll_diagnostics.cpp app/pixel_scroll_filter.h app/pixel_scroll_filter.cpp)
target_link_libraries(ex-agent-code PRIVATE ex_agent_chat ex_agent_chatplugin Qt6::Quick Qt6::QuickControls2)
set_target_properties(ex-agent-code PROPERTIES
    MACOSX_BUNDLE_BUNDLE_NAME "EX Agent Code"
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.exbusiness.agentcode"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
)
install(TARGETS ex-agent-code BUNDLE DESTINATION . RUNTIME DESTINATION bin)
