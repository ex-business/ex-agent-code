import QtQuick
import ExAgent.Chat

Item {
    property var block: ({})
    property ChatController controller
    property string messageId: ""
    property int blockIndex: 0
    property var outerWheel: null
    property bool detached: false
    signal linkActivated(string link)
}
