pragma ComponentBehavior: Bound
import QtQuick
import ExAgent.App as Design

Behavior {
    enabled: Design.Theme.motionEnabled && Design.InteractionMotion.allowed
    property Design.MotionRegistration registration: Design.MotionRegistration { running: rotationMotion.running }
    property Connections interruption: Connections {
        target: Design.InteractionMotion
        function onInterrupted() { if (rotationMotion.running) rotationMotion.complete() }
    }
    NumberAnimation { id: rotationMotion; duration: Design.Theme.disclosureDuration; easing.type: Easing.OutCubic }
}
