pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Shapes
import ExAgent.App as Design

Item {
    id: root
    property string name: "plus"
    property color color: Design.Theme.secondary
    implicitWidth: 18
    implicitHeight: 18
    // 由场景图绘制曲线，避免小尺寸 Canvas 位图被 Retina 放大后发虚。
    readonly property var paths: ({
        plus: "M12 5V19M5 12H19",
        select: "M4 9V4H9M15 4H20V9M20 15V20H15M9 20H4V15M8 8H16V16H8Z",
        arrow: "M12 19V5M6 11L12 5L18 11",
        down: "M7 10L12 15L17 10",
        right: "M9 6L15 12L9 18",
        sidebar: "M3 4H21V20H3ZM9 4V20",
        search: "M17 10.5A6.5 6.5 0 1 1 4 10.5A6.5 6.5 0 1 1 17 10.5M15.5 15.5L21 21",
        new: "M19 13V20H4V5H11M10 14L11 10L19 2L22 5L14 13Z",
        folder: "M3 7V20H21V7H12L10 4H3Z",
        copy: "M8 8H20V21H8ZM15 4H5V16",
        check: "M5 12L10 17L20 6",
        terminal: "M5 6L10 11L5 16M13 17H20",
        sun: "M16 12A4 4 0 1 1 8 12A4 4 0 1 1 16 12M12 3V5M12 19V21M3 12H5M19 12H21M5.6 5.6L7 7M17 17L18.4 18.4M5.6 18.4L7 17M17 7L18.4 5.6",
        chat: "M5 4H20V17H10L4 21V5Z",
        dots: "M7 12A1 1 0 1 1 5 12A1 1 0 1 1 7 12M13 12A1 1 0 1 1 11 12A1 1 0 1 1 13 12M19 12A1 1 0 1 1 17 12A1 1 0 1 1 19 12",
        home: "M3 11L12 3L21 11V21H15V14H9V21H3Z",
        clock: "M21 12A9 9 0 1 1 3 12A9 9 0 1 1 21 12M12 6V12L8 15",
        grid: "M4 4H10V10H4ZM14 4H20V10H14ZM4 14H10V20H4ZM14 14H20V20H14Z",
        file: "M5 3H15L20 8V21H5ZM15 3V8H20",
        mic: "M9 6A3 3 0 0 1 15 6V12A3 3 0 0 1 9 12ZM5 11C5 21 19 21 19 11M12 19V22",
        wave: "M5 9.5V14.5M8.5 6.5V17.5M12 4V20M15.5 7.5V16.5M19 9.5V14.5",
        speaker: "M4 9H8L13 5V19L8 15H4ZM17 8C21 10 21 14 17 16",
        back: "M19 12H5M10 7L5 12L10 17",
        close: "M6 6L18 18M18 6L6 18"
    })
    Shape {
        width: 24; height: 24
        preferredRendererType: Shape.CurveRenderer
        transform: Scale { xScale: root.width / 24; yScale: root.height / 24 }
        ShapePath {
            strokeColor: root.color
            strokeWidth: 1.65
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            PathSvg { path: root.paths[root.name] || root.paths.plus }
        }
    }
}
