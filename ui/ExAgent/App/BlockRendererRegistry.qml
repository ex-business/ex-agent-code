pragma Singleton
import QtQuick

// 渲染器白名单由宿主编译登记，事件中不允许传QML地址或脚本。
QtObject {
    function values(sequence: var, limit: int): var {
        const result = []
        // QVariantList在QML中可能是序列包装对象，不能用Array.isArray误判为空。
        if (!sequence || typeof sequence.length !== "number") return result
        for (let index = 0; index < Math.min(limit, sequence.length); ++index) result.push(sequence[index])
        return result
    }
    function sourceFor(kind: int): url {
        switch (kind) {
        case 1: return Qt.resolvedUrl("components/blocks/TextBlockRenderer.qml")
        case 2: case 3: case 7: return Qt.resolvedUrl("components/blocks/CodeBlockRenderer.qml")
        case 4: return Qt.resolvedUrl("components/blocks/ChartBlockRenderer.qml")
        case 5: return Qt.resolvedUrl("components/blocks/ImageBlockRenderer.qml")
        case 6: return Qt.resolvedUrl("components/blocks/ImageSequenceRenderer.qml")
        default: return Qt.resolvedUrl("components/blocks/UnknownBlockRenderer.qml")
        }
    }
    function labelFor(kind: int): string {
        return ({1:"正文",2:"代码",3:"终端输出",4:"图表",5:"图片",6:"图片序列",7:"差异"})[kind] || "不支持的内容"
    }
}
