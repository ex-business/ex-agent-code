pragma Singleton
import QtQml

// 常规启动没有诊断接收端，不采集内容身份或正文。
QtObject {
    property var sink: null
    function wheelMotion(phase: int, position: real, velocity: real, destination: real, input: real) {
        if (sink) sink.recordWheelMotion(phase, position, velocity, destination, input)
    }
    function layoutAnchor(correction: real, drift: real) {
        if (sink) sink.recordLayoutAnchor(correction, drift)
    }
    function contentPhase(loader: QtObject, kind: int, phase: int) {
        if (sink) sink.recordContentPhase(loader, kind, phase)
    }
}
