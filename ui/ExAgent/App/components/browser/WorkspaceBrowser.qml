pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ExAgent.Chat
import ExAgent.App as Design

Item {
    id: browser
    required property string workspaceId
    required property string sessionId
    signal elementPicked(string session, var details, var image)
    required property bool writable
    property var profiles: []
    property string selectedProfile: ""
    property var currentPages: null
    property var cached: []
    readonly property bool available: writable && !!workspaceId
    property bool initialized: false
    BrowserCatalog { id: catalog }
    Component {
        id: pageFactory
        Design.BrowserPages {
            anchors.fill: parent
            sessionId: browser.sessionId
            onElementPicked: (session, details, image) => browser.elementPicked(session,details,image)
        }
    }
    // 相邻工作树保留活页和前后导航；超出三个分区时按最近使用顺序释放渲染组件。
    function activate() {
        if (!initialized) return
        if (currentPages) { currentPages.visible=false; currentPages.foreground=false }
        currentPages=null
        if (!visible || !available) return
        profiles=catalog.profiles(workspaceId)
        selectedProfile=catalog.selected(workspaceId)
        const key=catalog.partition(workspaceId,selectedProfile)
        if (!key) return
        const list=cached.slice()
        const index=list.findIndex(group=>group.partition===key)
        const group=index>=0 ? list.splice(index,1)[0] : pageFactory.createObject(body,{partition:key,catalog:catalog})
        if (!group) return
        list.push(group); cached=list
        currentPages=group; group.visible=true; group.foreground=true
        if (list.length>3) { const oldest=list.shift(); cached=list.slice(); oldest.destroy() }
    }
    function choose(id: string) { if (available && catalog.select(workspaceId,id)) activate() }
    Component.onCompleted: { initialized=true; activate() }
    onWorkspaceIdChanged: { if (initialized) createProfile.close(); activate() }
    onAvailableChanged: activate()
    onVisibleChanged: activate()
    ColumnLayout {
        anchors.fill:parent; anchors.margins:8; spacing:5
        RowLayout {
            visible:browser.available; Layout.fillWidth:true; spacing:5
            Label { text:Design.UiText.get("浏览器配置"); color:Design.Theme.secondary; font.pixelSize:12 }
            Design.DialogSelect {
                id: picker; Layout.fillWidth:true; Layout.minimumWidth:0
                model:browser.profiles.map(row=>row.id==="default" ? Design.UiText.get("默认配置") : row.name)
                currentIndex:browser.profiles.findIndex(row=>row.id===browser.selectedProfile)
                onActivated: i=> { if (i>=0 && i<browser.profiles.length) browser.choose(browser.profiles[i].id) }
            }
            Design.IconButton { iconName:"plus"; tooltip:Design.UiText.get("新建浏览器配置"); onClicked:{ name.clear(); createProfile.problem=""; createProfile.open(); name.forceActiveFocus() } }
        }
        Item {
            id: body; Layout.fillWidth:true; Layout.fillHeight:true
            Label { anchors.centerIn:parent; visible:!browser.available; text:Design.UiText.get(browser.writable ? "请先打开工作树" : "请在可写工作区窗口打开浏览器"); color:Design.Theme.secondary }
        }
    }
    Design.WorkbenchDialog {
        id:createProfile; parent:Overlay.overlay; anchors.centerIn:Overlay.overlay; modal:true
        title:"新建浏览器配置"; sizeClass:Design.WorkbenchDialog.Compact
        property string problem:""
        function submit() {
            if (!browser.available) return
            const id=catalog.create(browser.workspaceId,name.text)
            if (!id) { problem=Design.UiText.get("请输入不同的配置名称"); return }
            close(); browser.choose(id)
        }
        contentItem:ColumnLayout {
            Design.DialogInput { id:name; Layout.fillWidth:true; maximumLength:64; placeholderText:Design.UiText.get("配置名称"); onAccepted:createProfile.submit() }
            Label { visible:!!text; text:createProfile.problem; color:Design.Theme.warning }
        }
        footer:Design.WorkbenchDialogActions {
            primaryText:"创建"; primaryEnabled:!!name.text.trim(); onPrimaryRequested:createProfile.submit(); onCancelled:createProfile.close()
        }
    }
}
