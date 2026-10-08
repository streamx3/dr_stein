// SPDX-License-Identifier: MIT
// Dr Stein: the Qt Quick front-end of libstein.
//
//   dr_stein [image files...]
//
// Environment, for scripted runs and screenshots:
//   DRSTEIN_VIEW=topology|hex|browse|image|profiles|tools|partitions   start on that view
//   DRSTEIN_SCREENSHOT=<file.png>    grab the window after DRSTEIN_DELAY ms (default 2500), save it, quit
//   DRSTEIN_SCRIPT=<javascript>      evaluated in Main.qml's scope 1 s after start (smoke runs)
#include "device_watcher.hpp"
#include "file_browser.hpp"
#include "workspace.hpp"

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlExpression>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTimer>

#include <QDebug>

#include <cstdlib>

int main(int argc, char* argv[]) {
    QGuiApplication::setApplicationName("Dr Stein");
    QGuiApplication::setApplicationDisplayName("Dr Stein");
    QGuiApplication::setOrganizationName("dr_stein");
    QGuiApplication::setOrganizationDomain("dr-stein.local");
    QGuiApplication::setApplicationVersion(DRSTEIN_VERSION_STRING);
    QGuiApplication app(argc, argv);
    // Basic has no look of its own: every control is drawn by our components from Theme.
    QQuickStyle::setStyle("Basic");

    QQmlApplicationEngine engine;
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app, [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.loadFromModule("DrStein", "Main");
    if (engine.rootObjects().isEmpty()) return 1;

    drstein::ui::Workspace* workspace = drstein::ui::Workspace::instance();
    if (const char* view = std::getenv("DRSTEIN_VIEW"); view && *view) workspace->setView(QString::fromUtf8(view));
    const QStringList args = QCoreApplication::arguments();
    QString last;
    for (int i = 1; i < args.size(); ++i)
        if (!args[i].startsWith('-')) {
            workspace->addImagePath(args[i]);
            last = args[i];
        }
    if (!last.isEmpty()) workspace->openImagePath(last);
    // Disks and mounts changing under us re-list the sidebar (and re-read the open disk).
    auto* watcher = new drstein::ui::DeviceWatcher(&app);
    QObject::connect(watcher, &drstein::ui::DeviceWatcher::devicesChanged, workspace, [workspace](const QString& what) {
        if (std::getenv("DRSTEIN_SCRIPT")) qInfo().noquote() << "devices changed:" << what;
        workspace->refresh();
    });
    workspace->setHotplug(watcher->available());
    // Mounts made by the app end with it; the singleton's destructor never runs.
    QObject::connect(&app, &QGuiApplication::aboutToQuit, &app, [workspace] {
        workspace->mounts().unmountAll();
        drstein::ui::FileBrowser::cleanupStaging();   // copies staged for drag-out
    });

    if (const char* script = std::getenv("DRSTEIN_SCRIPT"); script && *script) {
        const QString code = QString::fromUtf8(script);
        QTimer::singleShot(1000, &app, [&engine, code] {
            QObject* root = engine.rootObjects().first();
            QQmlExpression expr(QQmlEngine::contextForObject(root), root, code);
            bool undefined = false;
            expr.evaluate(&undefined);
            if (expr.hasError()) qWarning().noquote() << "DRSTEIN_SCRIPT:" << expr.error().toString();
        });
    }
    if (const char* shot = std::getenv("DRSTEIN_SCREENSHOT"); shot && *shot) {
        const QString file = QString::fromUtf8(shot);
        int delay = 2500;
        if (const char* d = std::getenv("DRSTEIN_DELAY"); d && *d) delay = std::atoi(d);
        QTimer::singleShot(delay, &app, [&engine, file] {
            auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
            if (window) window->grabWindow().save(file);
            QCoreApplication::quit();
        });
    }
    return app.exec();
}
