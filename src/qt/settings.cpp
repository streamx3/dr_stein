// SPDX-License-Identifier: MIT
#include "settings.hpp"

#include <QDir>
#include <QStandardPaths>

namespace drstein::ui {

namespace {
Settings* g_instance = nullptr;
}

Settings::Settings(QObject* parent) : QObject(parent), m_settings("dr_stein", "dr_stein") {
    m_palette = QString::fromUtf8(qgetenv("DRSTEIN_PALETTE"));
    m_scheme = QString::fromUtf8(qgetenv("DRSTEIN_SCHEME"));
    if (m_palette.isEmpty()) m_palette = m_settings.value("ui/palette", "teal").toString();
    if (m_scheme.isEmpty()) m_scheme = m_settings.value("ui/colorScheme", "dark").toString();
}

Settings* Settings::instance() {
    if (!g_instance) g_instance = new Settings();
    return g_instance;
}

Settings* Settings::create(QQmlEngine*, QJSEngine*) {
    Settings* s = instance();
    QQmlEngine::setObjectOwnership(s, QQmlEngine::CppOwnership);
    return s;
}

QString Settings::palette() const { return m_palette; }
void Settings::setPalette(const QString& p) {
    if (p == m_palette) return;
    m_palette = p;
    m_settings.setValue("ui/palette", p);
    Q_EMIT paletteChanged();
}

QString Settings::colorScheme() const { return m_scheme; }
void Settings::setColorScheme(const QString& s) {
    if (s == m_scheme) return;
    m_scheme = s;
    m_settings.setValue("ui/colorScheme", s);
    Q_EMIT colorSchemeChanged();
}

bool Settings::expertMode() const { return m_settings.value("ui/expertMode", false).toBool(); }
void Settings::setExpertMode(bool on) {
    if (on == expertMode()) return;
    m_settings.setValue("ui/expertMode", on);
    Q_EMIT expertModeChanged();
}

bool Settings::customTitleBar() const {
    if (const QByteArray env = qgetenv("DRSTEIN_CHROME"); !env.isEmpty()) return env != "native";
    return m_settings.value("ui/customTitleBar", true).toBool();
}
void Settings::setCustomTitleBar(bool on) {
    if (on == customTitleBar()) return;
    m_settings.setValue("ui/customTitleBar", on);
    Q_EMIT customTitleBarChanged();
}

bool Settings::interactDuringJobs() const { return m_settings.value("ui/interactDuringJobs", false).toBool(); }
void Settings::setInteractDuringJobs(bool on) {
    if (on == interactDuringJobs()) return;
    m_settings.setValue("ui/interactDuringJobs", on);
    Q_EMIT interactDuringJobsChanged();
}

bool Settings::decimalSizes() const { return m_settings.value("ui/decimalSizes", true).toBool(); }
void Settings::setDecimalSizes(bool on) {
    if (on == decimalSizes()) return;
    m_settings.setValue("ui/decimalSizes", on);
    Q_EMIT decimalSizesChanged();
}

QString Settings::profilesDir() const {
    if (const QByteArray env = qgetenv("DRSTEIN_PROFILES_DIR"); !env.isEmpty()) return QString::fromUtf8(env);
    const QString def = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + "/profiles";
    return m_settings.value("profiles/dir", def).toString();
}
void Settings::setProfilesDir(const QString& dir) {
    if (dir == profilesDir()) return;
    m_settings.setValue("profiles/dir", dir);
    Q_EMIT profilesDirChanged();
}

QStringList Settings::recentImages() const { return m_settings.value("images/recent").toStringList(); }
void Settings::addRecentImage(const QString& path) {
    QStringList list = recentImages();
    list.removeAll(path);
    list.prepend(path);
    while (list.size() > 12) list.removeLast();
    m_settings.setValue("images/recent", list);
    Q_EMIT recentImagesChanged();
}
void Settings::forgetRecentImage(const QString& path) {
    QStringList list = recentImages();
    if (!list.removeAll(path)) return;
    m_settings.setValue("images/recent", list);
    Q_EMIT recentImagesChanged();
}

int Settings::sidebarWidth() const { return m_settings.value("ui/sidebarWidth", 264).toInt(); }
void Settings::setSidebarWidth(int w) {
    if (w == sidebarWidth()) return;
    m_settings.setValue("ui/sidebarWidth", w);
    Q_EMIT sidebarWidthChanged();
}

QString Settings::lastImageDir() const { return m_settings.value("images/lastDir", QDir::homePath()).toString(); }
void Settings::setLastImageDir(const QString& dir) {
    if (dir == lastImageDir()) return;
    m_settings.setValue("images/lastDir", dir);
    Q_EMIT lastImageDirChanged();
}

} // namespace drstein::ui
