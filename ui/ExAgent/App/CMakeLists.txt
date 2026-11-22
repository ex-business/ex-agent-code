set_source_files_properties(Theme.qml BlockRendererRegistry.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
qt_add_qml_module(ex_agent_workbench
    STATIC
    URI ExAgent.App
    VERSION 1.0
    QML_FILES
        Main.qml Theme.qml BlockRendererRegistry.qml
        components/BlockRenderer.qml components/BlockWindowManager.qml
        components/media/MediaSession.qml components/media/MediaButton.qml
        components/media/MediaCanvas.qml components/media/MediaPreview.qml
        components/media/MediaControls.qml components/media/MediaThumbnails.qml components/media/MediaViewer.qml
        components/blocks/BlockRendererContent.qml
        components/blocks/TextBlockRenderer.qml components/blocks/CodeBlockRenderer.qml
        components/blocks/ChartBlockRenderer.qml components/blocks/ImageBlockRenderer.qml
        components/blocks/ImageSequenceRenderer.qml components/blocks/UnknownBlockRenderer.qml
        components/Icon.qml components/IconButton.qml
        components/ContextMenu.qml components/ContextMenuItem.qml
        components/Sidebar.qml components/Composer.qml
        components/ComposerToolbar.qml components/MessageActions.qml
        components/UserMessageBubble.qml
        components/SelectionSurface.qml components/SelectableText.qml components/SendButton.qml
        components/ChatScrollView.qml components/ChatSelectionLayer.qml components/ChatSelectionPart.qml
        components/SelectionAutoScroll.qml
        components/WorkspaceTabs.qml components/WorkbenchTitleBar.qml components/FilePreview.qml
        components/ChatMenu.qml
        components/MarkdownText.qml
        components/ToolActivityRow.qml components/MessageAttachments.qml
        components/ImagePreview.qml components/ChangesSummary.qml
        components/MessageContent.qml
        components/MessageDelegate.qml components/CodeBlock.qml
        components/EmptyState.qml
        components/ChangedFilesCard.qml components/NavigationRail.qml
        components/FilePanel.qml
        components/SmoothWheel.qml
        components/ChatOverview.qml
)
target_link_libraries(ex_agent_workbench PUBLIC ex_agent_chat Qt6::Quick Qt6::QuickControls2)
target_link_libraries(ex-agent-code PRIVATE ex_agent_workbenchplugin)
