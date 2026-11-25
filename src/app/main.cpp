#include "application/chat/chat_controller.h"
#include "plugins/computer_use/computer_cli.h"
#include "infrastructure/storage/history_migration.h"
#include <QStandardPaths>
#include "plugins/workspace_viewers/workspace_preview.h"
#include "plugins/workspace_viewers/html/safe_web_profile.h"
#include <QtWebEngineQuick>
#include "plugins/git_worktree/git_worktree_provider.h"
#include "scroll_diagnostics.h"
#include "profiled_application.h"
#include "pixel_scroll_filter.h"
#include "disclosure_input_filter.h"
#include "services/local/service_worker.h"

#include "agent/codex/thread_input.h"
#include <QJsonDocument>
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
    // 命令行只连接本机提供方，无需初始化窗口、聊天存储和 WebEngine。
    if (argc >= 2 && QByteArray(argv[1]) == "computer") {
        QCoreApplication cli(argc, argv);
        return exagent::computer::runCli(cli.arguments().mid(2));
    }
    if (argc == 3 && QByteArray(argv[1]) == "--continue-codex-terminal") {
        QCoreApplication app(argc,argv);
        app.setOrganizationName("EX Business"); app.setApplicationName("EX Agent Code");
        return exagent::codex::terminalContinuation(QJsonDocument::fromJson(QByteArray::fromBase64(argv[2])).toVariant().toMap());
    }
    // 后台与IDE共用发行包，但进程入口、事件循环和生命周期独立。
    for (int index = 1; index < argc; ++index)
        if (QByteArray(argv[index]) == "--ipc-service" || QByteArray(argv[index]) == "--agent-worker") return exagent::runServiceWorker(argc, argv);
    // 诊断用渲染循环对比开关，只影响本次启动，不修改用户环境。
    for (int index = 1; index < argc; ++index) {
        if (QByteArray(argv[index]) == "--render-loop-basic") qputenv("QSG_RENDER_LOOP", "basic");
    }
    exagent::registerWorkspaceWebScheme();
    QtWebEngineQuick::initialize();
    ProfiledApplication app(argc, argv);
    QCoreApplication::setOrganizationName("EX Business");
    QCoreApplication::setApplicationName("EX Agent Code");
    QCoreApplication::setApplicationVersion("0.1.0");
    if (app.arguments().contains("--migrate-history")) {
        QString error;
        if (exagent::migrateHistory(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation), &error)) return 0;
        qCritical().noquote() << error;
        return 1;
    }
    QQuickStyle::setStyle("Basic");
    // 小字号界面采用系统字体栅格化，避免缩放后的距离场文字发虚。
    QQuickWindow::setTextRenderType(QQuickWindow::NativeTextRendering);
    QGuiApplication::styleHints()->setColorScheme(Qt::ColorScheme::Light);

    exagent::ChatController controller;
    controller.projects()->registerTaskProvider(new exagent::GitWorktreeProvider);
    QQmlApplicationEngine engine;
    exagent::installWorkspaceViewers(engine);
    const bool profiling = app.arguments().contains("--profile-scroll") || app.arguments().contains("--profile-disclosure");
    const QString diagnosticLabel = profiling ? QStringLiteral("滚动诊断 · PID %1").arg(app.applicationPid()) : QString();
    engine.setInitialProperties({{"controller", QVariant::fromValue(&controller)}, {"diagnosticLabel", diagnosticLabel}});
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app, [] {
        qCritical("聊天界面加载失败，请检查 QML 诊断。");
        QCoreApplication::exit(1);
    }, Qt::QueuedConnection);
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &controller, &exagent::ChatController::flush);
    engine.loadFromModule("ExAgent.App", "Main");
if (!engine.rootObjects().isEmpty()) {
        if (auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first())) {
            if (auto *motion = engine.singletonInstance<QObject *>("ExAgent.App", "InteractionMotion"))
                new DisclosureInputFilter(window, motion);
        }
    }
#ifdef Q_OS_MACOS
    if (!engine.rootObjects().isEmpty()) {
        if (auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first())) {
            configureMacWindowChrome(window);
            installMacInactiveHover(window);
            new PixelScrollFilter(window);
        }
    }
#endif
    if (profiling && !engine.rootObjects().isEmpty()) {
        if (auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first())) {
            auto *diagnostics = new ScrollDiagnostics(window);
            if (app.arguments().contains("--profile-disclosure")) {
                app.diagnostics = diagnostics;
                if (auto *trace = engine.singletonInstance<QObject *>("ExAgent.App", "PerformanceTrace"))
                    trace->setProperty("sink", QVariant::fromValue(static_cast<QObject *>(diagnostics)));
            }
            // 诊断订阅模型的真实展开变更，不在业务入口注入模拟点击。
            QObject::connect(controller.messages(), &QAbstractItemModel::dataChanged, diagnostics,
                [diagnostics, &controller](const QModelIndex &first, const QModelIndex &, const QList<int> &roles) {
                    if (roles.contains(exagent::MessageListModel::ExpandedRole))
                        diagnostics->recordDisclosure(controller.messages()->data(first, exagent::MessageListModel::ExpandedRole).toBool() ? 1 : 2);
                    else if (roles.contains(exagent::MessageListModel::GroupRole)) diagnostics->recordDisclosure(0);
                });
        }
    }
    return app.exec();
}
