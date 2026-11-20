// SPDX-License-Identifier: MIT
// Dr Stein: the Qt Quick front-end of libstein.
//
//   dr-stein [image files...]        ("Dr Stein" on macOS and Windows)
//
// Environment, for scripted runs and screenshots:
//   DRSTEIN_VIEW=topology|hex|browse|image|profiles|tools|partitions   start on that view
//   DRSTEIN_SCREENSHOT=<file.png>    grab the window after DRSTEIN_DELAY ms (default 2500), save it, quit
//   DRSTEIN_SCRIPT=<javascript>      evaluated in Main.qml's scope 1 s after start (smoke runs)
//   DRSTEIN_PREVIEW=0|1              file previews in Browse off or on for this run; the stored setting is left alone
//   DRSTEIN_BROWSE_LAYOUT=columns|list   Browse's layout for this run; neither it nor Browse's pane widths are stored
//   DRSTEIN_SOURCE=<source id>       after an elevated relaunch: select this disk or image and ask once for its passphrase
#include "app_icon.hpp"
#include "codicon_provider.hpp"
#include "device_watcher.hpp"
#include "file_browser.hpp"
#include "preview_decode.hpp"
#include "workspace.hpp"

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlExpression>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTimer>

#include <QDebug>

#include <chrono>
#include <cstdlib>
#include <memory>

#if defined(Q_OS_LINUX)
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <system_error>

namespace {

// The installed tree (/opt/dr-stein, cmake/DrSteinLinuxInstall.cmake) carries the Qt it was built
// with; the bin/qt.conf next to the binary marks it. Variables meant for another Qt, such as the
// distribution's, must not reach that one: its plugins and QML modules belong to a different Qt
// version and crash it or stop the window loading. The tree has one platform plugin, xcb (on a
// Wayland session the app runs through XWayland), so a QT_QPA_PLATFORM naming another one gets
// xcb to fall back to instead of an abort. Returns false when the app cannot start at all.
bool prepareBundledQt(int argc, char* argv[]) {
    std::error_code ec;
    const std::filesystem::path exe = std::filesystem::read_symlink("/proc/self/exe", ec);
    if (ec || !std::filesystem::exists(exe.parent_path() / "qt.conf", ec)) return true;   // a build tree
    for (const char* name : {"QT_PLUGIN_PATH", "QML_IMPORT_PATH", "QML2_IMPORT_PATH"}) unsetenv(name);
    const char* requested = std::getenv("QT_QPA_PLATFORM");
    std::string platform = requested ? requested : "";
    if (platform.find("xcb") == std::string::npos) {
        platform = platform.empty() ? std::string("xcb") : platform + ";xcb";
        setenv("QT_QPA_PLATFORM", platform.c_str(), 1);
    }
    bool displayArgument = false;
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "-display") == 0 || std::strcmp(argv[i], "--display") == 0) displayArgument = true;
    if (const char* display = std::getenv("DISPLAY"); (!display || !*display) && !displayArgument) {
        std::fputs("dr-stein: no X display (DISPLAY is not set). Dr Stein runs on X11, and on Wayland through XWayland.\n", stderr);
        return false;
    }
    return true;
}

}  // namespace
#endif

int main(int argc, char* argv[]) {
#if defined(Q_OS_LINUX)
    if (!prepareBundledQt(argc, argv)) return 1;
#endif
    QGuiApplication::setApplicationName("Dr Stein");
    QGuiApplication::setApplicationDisplayName("Dr Stein");
    QGuiApplication::setOrganizationName("dr_stein");
    QGuiApplication::setOrganizationDomain("dr-stein.local");
    QGuiApplication::setApplicationVersion(DRSTEIN_VERSION_STRING);
#if defined(Q_OS_UNIX) && !defined(Q_OS_DARWIN)
    // The window's application id (Wayland app_id, _GTK_APPLICATION_ID, _KDE_NET_WM_DESKTOP_FILE):
    // the installed dr-stein.desktop, so the shell ties the window to the launcher and its icon.
    QGuiApplication::setDesktopFileName("dr-stein");
#endif
    QGuiApplication app(argc, argv);
    QGuiApplication::setWindowIcon(drstein::ui::appIcon());
    // Basic has no look of its own: every control is drawn by our components from Theme. The
    // Linux install ships no other style (cmake/DrSteinLinuxInstall.cmake, DRSTEIN_QT_STYLE).
    QQuickStyle::setStyle("Basic");

    // On the heap: it goes before the preview workers are waited for (the end of main).
    auto engineOwner = std::make_unique<QQmlApplicationEngine>();
    QQmlApplicationEngine& engine = *engineOwner;
    engine.addImageProvider("codicon", new drstein::ui::CodiconProvider);   // Linux caption buttons
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
    const int code = app.exec();
    // The QML objects go first: each FileBrowser tells its preview worker to stop. A worker may
    // still be in a decode, inside Qt, which the static destructors run by a return from main
    // take down under it (a crash on quit). It gets a moment; then the process ends without them.
    // So does an SVG drawing that was given up: it may run on for minutes, so it gets less.
    engineOwner.reset();
    if (!drstein::core::PreviewLoader::waitForWorkers(std::chrono::seconds(1)) || !drstein::ui::waitForSvgDrawings(std::chrono::milliseconds(250)))
        std::_Exit(code);
    return code;
}
