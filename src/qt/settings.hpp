// SPDX-License-Identifier: MIT
// Persistent preferences (QSettings). The Theme singleton reads palette and
// colour scheme from here; the views read expertMode.
#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QSettings>
#include <QStringList>

namespace drstein::ui {

class Settings : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(QString palette READ palette WRITE setPalette NOTIFY paletteChanged)             // "teal" | "blurple"
    Q_PROPERTY(QString colorScheme READ colorScheme WRITE setColorScheme NOTIFY colorSchemeChanged)   // "dark" | "light" | "system"
    Q_PROPERTY(bool expertMode READ expertMode WRITE setExpertMode NOTIFY expertModeChanged)
    Q_PROPERTY(bool customTitleBar READ customTitleBar WRITE setCustomTitleBar NOTIFY customTitleBarChanged)   // applies at next launch
    Q_PROPERTY(bool interactDuringJobs READ interactDuringJobs WRITE setInteractDuringJobs NOTIFY interactDuringJobsChanged)   // expert: keep the UI live while a job runs
    Q_PROPERTY(bool decimalSizes READ decimalSizes WRITE setDecimalSizes NOTIFY decimalSizesChanged)
    Q_PROPERTY(QString profilesDir READ profilesDir WRITE setProfilesDir NOTIFY profilesDirChanged)
    Q_PROPERTY(QStringList recentImages READ recentImages NOTIFY recentImagesChanged)
    Q_PROPERTY(int sidebarWidth READ sidebarWidth WRITE setSidebarWidth NOTIFY sidebarWidthChanged)
    Q_PROPERTY(QString lastImageDir READ lastImageDir WRITE setLastImageDir NOTIFY lastImageDirChanged)

public:
    static Settings* instance();
    static Settings* create(QQmlEngine*, QJSEngine*);

    QString palette() const;
    void setPalette(const QString& p);
    QString colorScheme() const;
    void setColorScheme(const QString& s);
    bool expertMode() const;
    void setExpertMode(bool on);
    bool customTitleBar() const;
    void setCustomTitleBar(bool on);
    bool interactDuringJobs() const;
    void setInteractDuringJobs(bool on);
    bool decimalSizes() const;
    void setDecimalSizes(bool on);
    QString profilesDir() const;
    void setProfilesDir(const QString& dir);
    QStringList recentImages() const;
    Q_INVOKABLE void addRecentImage(const QString& path);
    Q_INVOKABLE void forgetRecentImage(const QString& path);
    int sidebarWidth() const;
    void setSidebarWidth(int w);
    QString lastImageDir() const;
    void setLastImageDir(const QString& dir);

Q_SIGNALS:
    void paletteChanged();
    void colorSchemeChanged();
    void expertModeChanged();
    void customTitleBarChanged();
    void interactDuringJobsChanged();
    void decimalSizesChanged();
    void profilesDirChanged();
    void recentImagesChanged();
    void sidebarWidthChanged();
    void lastImageDirChanged();

private:
    explicit Settings(QObject* parent = nullptr);
    mutable QSettings m_settings;
    // DRSTEIN_PALETTE / DRSTEIN_SCHEME seed these at start-up (the elevated relaunch
    // carries the look across); a change in Settings replaces them for this run.
    QString m_palette, m_scheme;
};

} // namespace drstein::ui
