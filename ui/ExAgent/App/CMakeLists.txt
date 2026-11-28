set_source_files_properties(Theme.qml UiText.qml InteractionMotion.qml PerformanceTrace.qml BlockRendererRegistry.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
qt_add_qml_module(ex_agent_workbench
    STATIC
    URI ExAgent.App
    VERSION 1.0
    QML_FILES
        components/SharingLifecycleSection.qml
        components/MobileAccessDialog.qml
        components/SshWorkspaceDialog.qml
        components/DialogTextArea.qml components/BotManagerDialog.qml components/BotSessionBar.qml
        components/PluginManagerDialog.qml
        components/SupervisionDialog.qml
        components/ProjectTaskDialog.qml components/WorktreeResourcesDialog.qml
        components/SessionMailDialog.qml
        components/docking/PanelTree.js components/docking/DockSurface.qml components/docking/DockPanel.qml components/docking/PanelMoveDialog.qml components/docking/LegacyPanelLayout.qml
        Main.qml Theme.qml UiText.qml InteractionMotion.qml PerformanceTrace.qml BlockRendererRegistry.qml
        components/PressFeedback.qml components/DisclosureHeight.qml components/DisclosureEntrance.qml components/MotionRegistration.qml components/DisclosureRotation.qml
        components/WindowEntrance.qml components/BlockHeader.qml components/ActivityGroupHeader.qml
        components/BlockRenderer.qml components/BlockWindowManager.qml
        components/media/MediaSession.qml components/media/MediaButton.qml
        components/media/ImageTile.qml components/media/MediaToolButton.qml components/media/MediaViewerChrome.qml
        components/media/MediaCanvas.qml components/media/MediaPreview.qml
        components/media/MediaControls.qml components/media/MediaThumbnails.qml components/media/MediaViewer.qml
        components/blocks/BlockRendererContent.qml
        components/blocks/TextBlockRenderer.qml components/blocks/CodeBlockRenderer.qml
        components/blocks/ChartBlockRenderer.qml components/blocks/ImageBlockRenderer.qml
        components/blocks/ImageSequenceRenderer.qml components/blocks/UnknownBlockRenderer.qml
        components/Icon.qml components/IconButton.qml
        components/ContextMenu.qml components/ContextMenuItem.qml
        components/ProjectGroupManageDialog.qml components/ProjectGroupDialog.qml components/FolderWorkspaceDialog.qml components/Sidebar.qml components/ProjectTreeRow.qml components/AgentApprovalDialog.qml components/WorkspaceActivityDialog.qml components/GlobalNavigation.qml components/browser/WorkspaceBrowser.qml components/browser/BrowserPages.qml components/browser/DesignPicker.qml components/JumpTargets.js components/QuickOpenDialog.qml components/NotificationInbox.qml components/NativeAgentSettings.qml components/AccountProfilesDialog.qml components/AgentSettings.qml components/ConversationTransferDialog.qml components/Composer.qml
        components/ExecutionIndicator.qml components/ComposerToolbar.qml components/MessageActions.qml
        components/UserMessageBubble.qml
        components/SelectionSurface.qml components/SelectableText.qml components/SendButton.qml
        components/MessageLayoutReservation.qml components/ScrollRenderPolicy.qml components/HistoryPagePreloader.qml components/ChatScrollView.qml components/ChatSelectionLayer.qml components/ChatSelectionPart.qml
        components/SelectionAutoScroll.qml
        components/WorkspaceTabs.qml components/WorkbenchTitleBar.qml components/WorkspaceEditor.qml components/WorkspaceTextPreview.qml components/WorkspaceMarkdownView.qml components/WorkspaceImageViewer.qml components/WorkbenchWindow.qml components/FilePreview.qml
        components/WorkspaceSourcePlugin.qml components/WorkspaceImagePlugin.qml components/WorkspaceViewerStatus.qml components/WorkspaceWebPlugin.qml components/WorkspaceTablePlugin.qml components/WorkspacePdfPlugin.qml components/WorkspaceNotebookPlugin.qml
        components/ChatMenu.qml components/SessionConsoleWindow.qml components/WorkbenchDialogActions.qml components/WorkbenchConfirmDialog.qml components/WorkbenchDialog.qml components/DialogAction.qml components/DialogInput.qml components/DialogSelect.qml components/TerminalSettings.qml components/TerminalPane.qml components/TerminalAction.qml components/TerminalHistoryDialog.qml components/AgentLauncher.qml components/AgentProfiles.js
        components/AgentActivityDialog.qml
        components/ShellPresentation.js components/BlockGeometry.js components/ProsePreparation.qml components/MarkdownText.qml components/TableRulesOverlay.qml
        components/ToolActivityRow.qml components/MessageAttachments.qml
        components/ImagePreview.qml components/ChangesSummary.qml
        components/MessageContent.qml
        components/MessageDelegate.qml components/CodeBlock.qml
        components/EmptyState.qml
        components/ChangedFilesCard.qml components/NavigationRail.qml
        components/FilePanel.qml components/WorkspaceGitChanges.qml components/WorkspaceTasksDialog.qml components/DiffScopeBar.qml components/GitActionBar.qml components/DiffReviewDialog.qml
        components/SmoothWheel.qml components/WheelKinetics.qml
        components/ChatOverview.qml
)
target_link_libraries(ex_agent_workbench PUBLIC ex_agent_chat Qt6::Quick Qt6::QuickControls2)
target_link_libraries(ex-agent-code PRIVATE ex_agent_workbenchplugin)
