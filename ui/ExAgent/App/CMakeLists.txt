set_source_files_properties(Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
qt_add_qml_module(ex_agent_workbench
    STATIC
    URI ExAgent.App
    VERSION 1.0
    QML_FILES
        Main.qml Theme.qml
        components/Icon.qml components/IconButton.qml
        components/ContextMenu.qml components/ContextMenuItem.qml
        components/Sidebar.qml components/Composer.qml
        components/MessageDelegate.qml components/CodeBlock.qml
        components/EmptyState.qml
        components/ChangedFilesCard.qml components/NavigationRail.qml
        components/FilePanel.qml
        components/SmoothWheel.qml
        components/ChatOverview.qml
)
target_link_libraries(ex_agent_workbench PUBLIC ex_agent_chat Qt6::Quick Qt6::QuickControls2)
target_link_libraries(ex-agent-code PRIVATE ex_agent_workbenchplugin)
