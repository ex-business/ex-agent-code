#include "application/chat/chat_controller.h"
#include "scroll_diagnostics.h"
#include "pixel_scroll_filter.h"
#include "services/local/service_worker.h"

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QStyleHints>
#ifdef Q_OS_MACOS
#include "platform/macos/window_chrome.h"
#include "platform/macos/inactive_hover.h"
#endif

int main(int argc, char *argv[])
{
    // 后台与IDE共用发行包，但进程入口、事件循环和生命周期独立。
    for (int index = 1; index < argc; ++index)
        if (QByteArray(argv[index]) == "--ipc-service") return exagent::runServiceWorker(argc, argv);
    // 诊断用渲染循环对比开关，只影响本次启动，不修改用户环境。
    for (int index = 1; index < argc; ++index) {
        if (QByteArray(argv[index]) == "--render-loop-basic") qputenv("QSG_RENDER_LOOP", "basic");
    }
    QGuiApplication app(argc, argv);
    QCoreApplication::setOrganizationName("EX Business");
    QCoreApplication::setApplicationName("EX Agent Code");
    QCoreApplication::setApplicationVersion("0.1.0");
    QQuickStyle::setStyle("Basic");
    // 小字号界面采用系统字体栅格化，避免缩放后的距离场文字发虚。
    QQuickWindow::setTextRenderType(QQuickWindow::NativeTextRendering);
    QGuiApplication::styleHints()->setColorScheme(Qt::ColorScheme::Light);

    exagent::ChatController controller;
    QQmlApplicationEngine engine;
    engine.setInitialProperties({{"controller", QVariant::fromValue(&controller)}});
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app, [] {
        qCritical("聊天界面加载失败，请检查 QML 诊断。");
        QCoreApplication::exit(1);
    }, Qt::QueuedConnection);
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &controller, &exagent::ChatController::flush);
    engine.loadFromModule("ExAgent.App", "Main");
#ifdef Q_OS_MACOS
    if (!engine.rootObjects().isEmpty()) {
        if (auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first())) {
            configureMacWindowChrome(window);
            installMacInactiveHover(window);
            new PixelScrollFilter(window);
        }
    }
#endif
    if (app.arguments().contains("--profile-scroll") && !engine.rootObjects().isEmpty()) {
        if (auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first())) new ScrollDiagnostics(window);
    }
    return app.exec();
}
