// SPDX-License-Identifier: MIT
#include "settings.hpp"

#include <QDir>
#include <QStandardPaths>

#include <algorithm>

namespace drstein::ui {

namespace {
Settings* g_instance = nullptr;

struct WidthRange {
    const char* key;
    int fallback, least, most;
};
constexpr WidthRange kTreeWidth{"ui/browseTreeWidth", 200, 150, 420};
constexpr WidthRange kPreviewWidth{"ui/browsePreviewWidth", 280, 220, 560};
constexpr WidthRange kColumnWidth{"ui/browseColumnWidth", 240, 140, 640};

QString layoutName(const QString& stored) { return stored == QLatin1String("list") ? QStringLiteral("list") : QStringLiteral("columns"); }

int storedWidth(const QSettings& s, const WidthRange& r) {
    bool ok = false;
    const int w = s.value(r.key, r.fallback).toInt(&ok);
    return ok ? std::clamp(w, r.least, r.most) : r.fallback;
}
} // namespace

Settings::Settings(QObject* parent) : QObject(parent), m_settings("dr_stein", "dr_stein") {
    m_palette = QString::fromUtf8(qgetenv("DRSTEIN_PALETTE"));
    m_scheme = QString::fromUtf8(qgetenv("DRSTEIN_SCHEME"));
    if (m_palette.isEmpty()) m_palette = m_settings.value("ui/palette", "teal").toString();
    if (m_scheme.isEmpty()) m_scheme = m_settings.value("ui/colorScheme", "dark").toString();
    const QByteArray preview = qgetenv("DRSTEIN_PREVIEW");
    m_previewFromEnv = !preview.isEmpty();
    m_previewFiles = m_previewFromEnv ? preview != "0" : m_settings.value("ui/previewFiles", true).toBool();
    const QByteArray layout = qgetenv("DRSTEIN_BROWSE_LAYOUT");
    m_browseFromEnv = !layout.isEmpty();
    m_browseLayout = layoutName(m_browseFromEnv ? QString::fromUtf8(layout) : m_settings.value("ui/browseLayout").toString());
    m_browseTreeWidth = storedWidth(m_settings, kTreeWidth);
    m_browsePreviewWidth = storedWidth(m_settings, kPreviewWidth);
    m_browseColumnWidth = storedWidth(m_settings, kColumnWidth);
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

bool Settings::previewFiles() const { return m_previewFiles; }
void Settings::setPreviewFiles(bool on) {
    if (on == m_previewFiles) return;
    m_previewFiles = on;
    if (!m_previewFromEnv) m_settings.setValue("ui/previewFiles", on);
    Q_EMIT previewFilesChanged();
}

void Settings::setBrowseLayout(const QString& layout) {
    const QString name = layoutName(layout);
    if (name == m_browseLayout) return;
    m_browseLayout = name;
    if (!m_browseFromEnv) m_settings.setValue("ui/browseLayout", name);
    Q_EMIT browseLayoutChanged();
}

void Settings::setBrowseTreeWidth(int w) {
    w = std::clamp(w, kTreeWidth.least, kTreeWidth.most);
    if (w == m_browseTreeWidth) return;
    m_browseTreeWidth = w;
    if (!m_browseFromEnv) m_settings.setValue(kTreeWidth.key, w);
    Q_EMIT browseTreeWidthChanged();
}

void Settings::setBrowsePreviewWidth(int w) {
    w = std::clamp(w, kPreviewWidth.least, kPreviewWidth.most);
    if (w == m_browsePreviewWidth) return;
    m_browsePreviewWidth = w;
    if (!m_browseFromEnv) m_settings.setValue(kPreviewWidth.key, w);
    Q_EMIT browsePreviewWidthChanged();
}

void Settings::setBrowseColumnWidth(int w) {
    w = std::clamp(w, kColumnWidth.least, kColumnWidth.most);
    if (w == m_browseColumnWidth) return;
    m_browseColumnWidth = w;
    if (!m_browseFromEnv) m_settings.setValue(kColumnWidth.key, w);
    Q_EMIT browseColumnWidthChanged();
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
bool Settings::sidebarVisible() const { return m_settings.value("ui/sidebarVisible", true).toBool(); }
void Settings::setSidebarVisible(bool on) {
    if (on == sidebarVisible()) return;
    m_settings.setValue("ui/sidebarVisible", on);
    Q_EMIT sidebarVisibleChanged();
}
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
