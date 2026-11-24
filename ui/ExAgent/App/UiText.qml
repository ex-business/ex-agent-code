pragma Singleton
import QtQuick
import ExAgent.Chat

QtObject {
    readonly property var catalog: AppearanceRegistry.strings
    function get(source: string): string { return catalog[source] || source }
}
