pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtWebEngine
import ExAgent.Chat
import ExAgent.App as Design

Item {
    id: pages
    required property string sessionId
    property bool picking: false
    signal elementPicked(string session, var details, var image)
    onForegroundChanged: if (!foreground) picking = false
    onCurrentChanged: picking = false
    onSessionIdChanged: picking = false
    required property string partition
    required property BrowserCatalog catalog
    property bool foreground: false
    property int current: -1
    property bool ready: false
    property var profileInstance: null
    property int viewRevision: 0
    readonly property var web: {
        const revision = viewRevision
        const slot = current >= 0 ? views.itemAt(current) : null
        return slot ? slot.item : null
    }
    property bool invalidAddress: false
    property string pageError: ""
    function focusAddress() { address.forceActiveFocus(); address.selectAll() }
    function restoreAddress() {
        // 离开编辑状态后地址栏必须反映真实页面，包括编辑期间发生的跨站跳转。
        const value = web ? web.url : ""
        address.text = catalog.allowed(value) ? value.toString() : ""
        invalidAddress = false
    }
    function persist() {
        if (!ready) return
        const urls=[]
        for (let i=0;i<tabs.count;i++) urls.push(tabs.get(i).lastUrl)
        catalog.save(partition,urls,current)
    }
    function activateTab(i: int) {
        if (i<0 || i>=tabs.count) return
        current=i; tabs.setProperty(i,"started",true)
        address.text=tabs.get(i).lastUrl; invalidAddress=false; pageError=""; persist()
    }
    function addTab(url: string) {
        tabs.append({initialUrl:url,lastUrl:url,caption:"",started:false})
        activateTab(tabs.count-1)
        if (!url) focusAddress()
    }
    function closeTab(i: int) {
        if (i<0 || i>=tabs.count) return
        const next=current>i ? current-1 : Math.min(current,tabs.count-2)
        tabs.remove(i)
        if (tabs.count) activateTab(next)
        else addTab("")
    }
    function navigate() {
        const url=catalog.address(address.text)
        invalidAddress = !url.toString()
        if (invalidAddress) return
        if (web) { pageError=""; web.url=url; web.forceActiveFocus() }
    }
    WebEngineProfilePrototype { id: prototype; storageName: pages.partition }
    ListModel { id: tabs }
    Component.onCompleted: {
        profileInstance=prototype.instance()
        if (!profileInstance) return
        const saved=catalog.restore(partition), urls=saved.urls || []
        for (const value of urls) {
            const url=catalog.allowed(value) ? value : ""
            tabs.append({initialUrl:url,lastUrl:url,caption:"",started:false})
        }
        if (tabs.count) activateTab(Math.min(Math.max(saved.active || 0,0),tabs.count-1))
        else addTab("")
        ready=true
    }
    ColumnLayout {
        anchors.fill: parent; spacing: 4
        RowLayout {
            Layout.fillWidth: true; spacing: 2
            ListView {
                id: strip; Layout.fillWidth: true; Layout.preferredHeight: 30
                orientation: ListView.Horizontal; clip: true; spacing: 2; model: tabs
                currentIndex: pages.current
                delegate: Rectangle {
                    id: tab
                    required property int index
                    required property string caption
                    required property string lastUrl
                    width: Math.min(160,Math.max(96,strip.width)); height: strip.height
                    radius: 5; color: index===pages.current ? Design.Theme.selected : "transparent"
                    RowLayout {
                        anchors.fill: parent; spacing: 0
                        ItemDelegate {
                            Layout.fillWidth: true; Layout.fillHeight: true
                            background: Item {}
                            contentItem: Text { text: tab.caption || tab.lastUrl || Design.UiText.get("新标签页"); color: Design.Theme.text; textFormat: Text.PlainText; elide: Text.ElideRight; verticalAlignment: Text.AlignVCenter; font.pixelSize: 12 }
                            onClicked: pages.activateTab(tab.index)
                        }
                        Design.IconButton { iconName:"close"; iconSize:12; implicitWidth:24; tooltip:Design.UiText.get("关闭标签页"); onClicked:pages.closeTab(tab.index) }
                    }
                }
            }
            Design.IconButton { iconName:"plus"; tooltip:Design.UiText.get("新标签页"); enabled: pages.ready; onClicked:pages.addTab("") }
        }
        RowLayout {
            Layout.fillWidth: true; spacing: 2
            Design.IconButton { iconName:"back"; tooltip:Design.UiText.get("后退"); enabled: !!pages.web && pages.web.canGoBack; onClicked:pages.web.goBack() }
            Design.IconButton { iconName:"forward"; tooltip:Design.UiText.get("前进"); enabled: !!pages.web && pages.web.canGoForward; onClicked:pages.web.goForward() }
            Design.IconButton { iconName:pages.web && pages.web.loading ? "close" : "refresh"; tooltip:Design.UiText.get(pages.web && pages.web.loading ? "停止加载" : "重新加载"); enabled: !!pages.web; onClicked:pages.web.loading ? pages.web.stop() : pages.web.reload() }
            Design.IconButton {
                iconName: "select"
                tooltip: Design.UiText.get(pages.picking ? "取消点选" : "点选网页元素")
                iconColor: pages.picking ? Design.Theme.accent : Design.Theme.secondary
                enabled: !!pages.web && !pages.web.loading && pages.catalog.allowed(pages.web.url)
                onClicked: { pages.pageError = ""; pages.picking = !pages.picking }
            }
            Design.DialogInput { id: address; Layout.fillWidth:true; Layout.minimumWidth:0; enabled: pages.ready
                placeholderText:Design.UiText.get("输入网址或 localhost 地址"); onAccepted:pages.navigate()
                onActiveFocusChanged: if (!activeFocus && pages.ready) pages.restoreAddress()
                Keys.onEscapePressed: { pages.restoreAddress(); if (pages.web) pages.web.forceActiveFocus() }
            }
        }
        Label { Layout.fillWidth:true; visible: !!text; text: !pages.profileInstance ? Design.UiText.get("浏览器配置不可用") : pages.invalidAddress ? Design.UiText.get("请输入有效的 HTTP 或 HTTPS 地址") : pages.pageError; color:Design.Theme.warning; wrapMode:Text.Wrap }
        Item {
            Layout.fillWidth:true; Layout.fillHeight:true
            Repeater {
                id: views; model:tabs
                onItemAdded: pages.viewRevision++
                onItemRemoved: pages.viewRevision++
                delegate: Loader {
                    id: slot
                    required property int index
                    required property string initialUrl
                    required property bool started
                    anchors.fill:parent
                    visible: index===pages.current
                    active: started && !!pages.profileInstance
                    sourceComponent: WebEngineView {
                        id: page
                        profile: pages.profileInstance
                        backgroundColor:Design.Theme.background
                        settings.localContentCanAccessFileUrls:false
                        settings.localContentCanAccessRemoteUrls:false
                        settings.unknownUrlSchemePolicy:WebEngineSettings.DisallowUnknownUrlSchemes
                        Component.onCompleted: if (slot.initialUrl) url=slot.initialUrl
                        onNavigationRequested: request => {
                            if (request.isMainFrame && request.url.toString()!=="about:blank" && !pages.catalog.allowed(request.url)) request.reject()
                        }
                        onNewWindowRequested: request => {
                            if (!pages.foreground || !slot.visible || !request.userInitiated || !pages.catalog.allowed(request.requestedUrl)) return
                            // 交给同配置的新页面承接原始导航，保留 POST 请求体和 opener 语义。
                            const index = tabs.count
                            pages.addTab("")
                            const target = views.itemAt(index)
                            if (target && target.item) {
                                request.openIn(target.item)
                                target.item.forceActiveFocus()
                            } else pages.closeTab(index)
                        }
                        onTitleChanged: if (slot.index>=0 && slot.index<tabs.count) tabs.setProperty(slot.index,"caption",title)
                        onUrlChanged: {
                            if (slot.index<0 || slot.index>=tabs.count) return
                            const value=pages.catalog.allowed(url) ? url.toString() : ""
                            if (slot.index===pages.current && !address.activeFocus) address.text=value
                            if (tabs.get(slot.index).lastUrl!==value) { tabs.setProperty(slot.index,"lastUrl",value); pages.persist() }
                        }
                        onLoadingChanged: info => {
                            if (slot.index===pages.current && info.status===WebEngineView.LoadFailedStatus)
                                pages.pageError=Design.UiText.get("页面加载失败")+" · "+info.errorString
                            else if (slot.index===pages.current && info.status===WebEngineView.LoadStartedStatus) pages.pageError=""
                        }
                    }
                }
            }
            Design.DesignPicker {
                anchors.fill: parent
                view: pages.web
                sessionId: pages.sessionId
                visible: pages.picking && pages.foreground && !!view
                onCancelled: pages.picking = false
                onRejected: reason => pages.pageError = reason
                onPicked: (session,details,image) => {
                    pages.picking = false
                    pages.pageError = ""
                    pages.elementPicked(session,details,image)
                }
            }
        }
    }
}
