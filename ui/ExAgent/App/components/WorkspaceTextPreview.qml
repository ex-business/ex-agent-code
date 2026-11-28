pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ExAgent.Chat
import ExAgent.App as Design

Flickable {
    id: preview
    required property WorkspaceController workspace
    required property ChatController controller
    objectName: "workspaceMarkdownPreview"
    signal sourceViewportChanged(int line, real fraction)
    signal layoutReady()
    signal anchorLocated(int line)
    property bool mappingReady: false
    property bool mappingLayoutDirty: true
    onWidthChanged: mappingLayoutDirty=true
    onHeightChanged: mappingLayoutDirty=true
    property int layoutGeneration: 0
    property bool syncingViewport: false
    property string pendingAnchor: ""
    property string shownPath: ""
    Component.onCompleted: shownPath=workspace.rootPath+"\n"+workspace.activePath
    property string anchorError: ""
    function sourceLocation() {
        for(let i=0;i<rendered.count;++i){
            const slot=rendered.itemAt(i)
            if(!slot || !slot.item || slot.y+slot.height<contentY-16)continue
            const map=slot.modelData.lineMap || []
            if(!map.length)return ({line:1,fraction:0})
            const y=Math.max(0,contentY-16-slot.y)
            if(slot.modelData.kind===1)return ({line:slot.modelData.sourceLine+slot.item.sourceRowAt(y),fraction:0})
            const pos=slot.item.textPositionAt(0,y)
            let line=map[0].line,position=map[0].position
            for(const entry of map){if(entry.position>pos)break;line=entry.line;position=entry.position}
            const rect=slot.item.textRectangle(position)
            return ({line:line,fraction:Math.max(0,Math.min(1,(y-rect.y)/Math.max(1,rect.height)))})
        }
        return ({line:1,fraction:0})
    }
    function prepareMappingLayout() {
        // 后台窗口也可能收到导航；只在发布和显式定位前完成待处理布局，不能依赖下一显示帧。
        if(!mappingLayoutDirty)return
        content.ensurePolished()
        for(let i=0;i<rendered.count;++i){const slot=rendered.itemAt(i);if(slot.item && slot.item.prepareSourceLayout)slot.item.prepareSourceLayout()}
        content.ensurePolished()
        mappingLayoutDirty=false
    }
    function reveal(index, position, fraction, codeRow) {
        syncingViewport=true
        prepareMappingLayout()
        const slot=rendered.itemAt(index)
        if(!slot || !slot.item){syncingViewport=false;return false}
        const y=slot.modelData.kind===1 ? slot.item.sourceRowY(codeRow) : slot.item.textRectangle(position).y
        const height=slot.modelData.kind===1 ? slot.item.lineHeight : slot.item.textRectangle(position).height
        contentY=Math.max(0,Math.min(Math.max(0,contentHeight-heightForViewport()),slot.y+16+y+fraction*height))
        Qt.callLater(()=>{preview.syncingViewport=false})
        return true
    }
    function heightForViewport(){return height}
    function scrollToSourceLine(line, fraction) {
        if(!mappingReady)return
        let index=-1,selected=null
        for(let i=0;i<rendered.count;++i){const slot=rendered.itemAt(i);for(const entry of (slot.modelData.lineMap||[])){if(entry.line>line)break;index=i;selected=entry}}
        if(index>=0)reveal(index,selected.position,fraction,selected.line-rendered.itemAt(index).modelData.sourceLine)
    }
    function openAnchor(name) {
        if(!name){contentY=0;return}
        if(prepared.state!==WorkspaceMarkdownModel.Ready || !mappingReady){pendingAnchor=name;return}
        pendingAnchor="";anchorError=""
        for(let i=0;i<rendered.count;++i){const slot=rendered.itemAt(i);for(const anchor of (slot.modelData.anchors||[]))if(anchor.name===name){if(reveal(i,anchor.position,0,0))anchorLocated(anchor.line);return}}
        anchorError=Design.UiText.get("未找到同页锚点")+": "+name
    }
    onContentYChanged: if(markdown && visible && mappingReady && !syncingViewport){const at=sourceLocation();sourceViewportChanged(at.line,at.fraction)}
    Connections {
        target: preview.workspace
        function resetFile(){
            const identity=preview.workspace.rootPath+"\n"+preview.workspace.activePath
            if(preview.shownPath!==identity){preview.shownPath=identity;preview.pendingAnchor="";preview.anchorError="";preview.mappingReady=false;++preview.layoutGeneration}
        }
        function onPreviewChanged(){resetFile()}
        function onFilesChanged(){resetFile()}
        function onAnchorRequested(name){
            const path=preview.workspace.rootPath+"\n"+preview.workspace.activePath
            if(preview.markdown)Qt.callLater(()=>{if(path===preview.workspace.rootPath+"\n"+preview.workspace.activePath)preview.openAnchor(name)})
        }
    }
    Connections {
        target: prepared
        function onChanged(){
            preview.mappingReady=false
            preview.mappingLayoutDirty=true
            const generation=++preview.layoutGeneration
            if(prepared.state===WorkspaceMarkdownModel.Ready)Qt.callLater(()=>Qt.callLater(()=>{
                if(generation!==preview.layoutGeneration || prepared.state!==WorkspaceMarkdownModel.Ready)return
                preview.prepareMappingLayout()
                preview.mappingReady=true
                if(preview.pendingAnchor)preview.openAnchor(preview.pendingAnchor)
                else preview.layoutReady()
            }))
        }
    }
    property bool markdown: false
    property string sourceText: workspace.source
    property bool pixelWheelEnabled: true
    property bool pixelWheelAnimated: Design.Theme.motionEnabled
    readonly property bool pixelWheelMoving: wheel.kineticRunning
    function pixelScrollBy(x: real,y: real): point { return Qt.point(0,wheel.consumePixels(y)) }
    signal pixelScrollStarted()
    clip: true; contentWidth: width; contentHeight: content.implicitHeight+32
    acceptedButtons: Qt.NoButton; boundsBehavior: Flickable.StopAtBounds
    Design.SmoothWheel { id: wheel; view: preview }
    ScrollBar.vertical: ScrollBar { width: 6; policy: ScrollBar.AsNeeded }
    WorkspaceMarkdownModel {
        id: prepared
        objectName: "workspaceMarkdownModel"
        active: preview.visible && preview.markdown
        source: preview.markdown ? preview.sourceText : ""
        rootPath: preview.workspace.rootPath
        filePath: preview.workspace.rootPath+"/"+preview.workspace.activePath
        dark: Design.Theme.dark
        previewWidth: Math.floor(Math.max(16,preview.width-32))
    }
    ColumnLayout {
        id: content
        onImplicitHeightChanged: preview.mappingLayoutDirty=true
        x: 16; y: 16; width: Math.max(1,preview.width-32); spacing: 14
        Label { visible: preview.markdown && !!prepared.error; text: Design.UiText.get(prepared.error); Layout.fillWidth: true; wrapMode: Text.Wrap; color: Design.Theme.warning }
        Label { visible: !!preview.anchorError; text: preview.anchorError; Layout.fillWidth: true; color: Design.Theme.warning; wrapMode: Text.Wrap }
        Repeater {
            id: rendered
            model: preview.markdown && prepared.state!==WorkspaceMarkdownModel.Failed ? prepared.blocks : [{kind:1,text:preview.sourceText,language:preview.workspace.language}]
            delegate: Loader {
                id: slot
                required property var modelData
                required property int index
                Layout.fillWidth: true
                sourceComponent: modelData.kind===1 ? code : prose
                Component { id: code; Design.CodeBlock { filePath: preview.workspace.activePath; blockIndex: slot.index; code: slot.modelData.text; knownLineCount: slot.modelData.codeLineCount === undefined ? -1 : slot.modelData.codeLineCount; language: slot.modelData.language; controller: preview.controller; maximumCodeHeight: Math.max(160,preview.height-90); outerWheel: wheel; onSourceViewportChanged: { if(!preview.syncingViewport){const at=preview.sourceLocation();preview.sourceViewportChanged(at.line,at.fraction)} } } }
                Component {
                    id: prose
                    Design.MarkdownText {
                        id: paragraph
                        block: slot.modelData; selectionView: preview
                        suppliedPresentation: slot.modelData
                        preparation: Design.ProsePreparation { slot: paragraph; source: "" }
                        onLinkActivated: link => {
                            if(link.startsWith("#")){let name=link.substring(1);try{name=decodeURIComponent(name)}catch(error){}preview.openAnchor(name)}
                            else preview.workspace.openLink(link,preview.workspace.rootPath+"/"+preview.workspace.activePath)
                        }
                    }
                }
            }
        }
    }
}
