// SPDX-License-Identifier: MIT
// The session: sources in the sidebar, the open one, its topology and the
// selected node. Opening probes on the job thread; everything else is
// synchronous on the GUI thread.
#pragma once

#include "drstein/core/mounts.hpp"
#include "drstein/core/opened.hpp"
#include "models.hpp"

#include <QObject>
#include <QQmlEngine>
#include <QStringList>
#include <QUrl>
#include <QVariantMap>

#include <map>
#include <memory>
#include <optional>

namespace drstein::ui {

class Workspace : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(drstein::ui::SourceListModel* sources READ sources CONSTANT)
    Q_PROPERTY(drstein::ui::TopologyModel* topology READ topology CONSTANT)
    Q_PROPERTY(drstein::ui::NodeDetailsObject* details READ details CONSTANT)
    Q_PROPERTY(bool elevated READ elevated CONSTANT)
    Q_PROPERTY(QString platformName READ platformName CONSTANT)
    Q_PROPERTY(QString view READ view WRITE setView NOTIFY viewChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(bool hasCurrent READ hasCurrent NOTIFY currentChanged)
    Q_PROPERTY(QString currentId READ currentId NOTIFY currentChanged)
    Q_PROPERTY(QString title READ title NOTIFY currentChanged)
    Q_PROPERTY(QString path READ path NOTIFY currentChanged)
    Q_PROPERTY(QString identityLine READ identityLine NOTIFY currentChanged)
    Q_PROPERTY(QString tableText READ tableText NOTIFY currentChanged)
    Q_PROPERTY(QString healthText READ healthText NOTIFY currentChanged)
    Q_PROPERTY(QString healthLevel READ healthLevel NOTIFY currentChanged)
    Q_PROPERTY(QString sizeText READ sizeText NOTIFY currentChanged)
    Q_PROPERTY(QString lastLbaText READ lastLbaText NOTIFY currentChanged)
    Q_PROPERTY(bool isImage READ isImage NOTIFY currentChanged)
    Q_PROPERTY(bool isDisk READ isDisk NOTIFY currentChanged)
    Q_PROPERTY(bool readOnly READ readOnly NOTIFY currentChanged)
    Q_PROPERTY(bool imageLocked READ imageLocked NOTIFY currentChanged)
    Q_PROPERTY(QStringList notes READ notes NOTIFY currentChanged)
    Q_PROPERTY(QVariantList segments READ segments NOTIFY currentChanged)
    Q_PROPERTY(QVariantMap currentError READ currentError NOTIFY currentChanged)
    Q_PROPERTY(bool hasCurrentError READ hasCurrentError NOTIFY currentChanged)
    Q_PROPERTY(QVariantList selectedPath READ selectedPathVariant NOTIFY selectionChanged)
    Q_PROPERTY(int selectedMetadataIndex READ selectedMetadataIndex NOTIFY selectionChanged)
    Q_PROPERTY(int selectedSegment READ selectedSegment NOTIFY selectionChanged)
    Q_PROPERTY(QString version READ version CONSTANT)
    Q_PROPERTY(bool hotplug READ hotplug NOTIFY hotplugChanged)              // the OS tells us about device changes
    Q_PROPERTY(bool uiLocked READ uiLocked NOTIFY uiLockedChanged)          // a job is writing or reading; only Cancel stays live
    Q_PROPERTY(QString restoreTargetId READ restoreTargetId NOTIFY restoreTargetChanged)

public:
    static Workspace* instance();
    static Workspace* create(QQmlEngine*, QJSEngine*);

    SourceListModel* sources() { return &m_sources; }
    TopologyModel* topology() { return &m_topology; }
    NodeDetailsObject* details() { return &m_details; }
    bool elevated() const;
    QString platformName() const;
    QString view() const { return m_view; }
    void setView(const QString& v);
    bool busy() const { return m_busy; }
    bool hasCurrent() const { return m_current.has_value(); }
    QString currentId() const;
    QString title() const;
    QString path() const;
    QString identityLine() const;
    QString tableText() const;
    QString healthText() const;
    QString healthLevel() const;
    QString sizeText() const;
    QString lastLbaText() const;
    bool isImage() const;
    bool isDisk() const;
    bool readOnly() const;
    bool imageLocked() const;
    QStringList notes() const;
    QVariantList segments() const { return m_segments; }
    QVariantMap currentError() const { return m_currentError; }
    bool hasCurrentError() const { return !m_currentError.isEmpty(); }
    QVariantList selectedPathVariant() const;
    int selectedMetadataIndex() const { return m_selectedMetadata; }
    int selectedSegment() const;
    QString version() const;

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void openImage(const QUrl& url);
    Q_INVOKABLE void openImagePath(const QString& path);
    Q_INVOKABLE void addImagePath(const QString& path);   // sidebar only, keeps the current source
    Q_INVOKABLE void closeImage(const QString& id);
    Q_INVOKABLE void select(const QString& id);
    Q_INVOKABLE void selectRow(int row);
    Q_INVOKABLE void selectPath(const QVariantList& path);
    Q_INVOKABLE void selectSegment(int segment);
    Q_INVOKABLE void unlock(const QString& passphrase);
    Q_INVOKABLE void reprobe();
    Q_INVOKABLE void reopenCurrent();   // after a permission fix: try the selected source again
    Q_INVOKABLE void computeUsage();
    Q_INVOKABLE QVariantMap sourceInfo(const QString& id) const;
    bool uiLocked() const;
    bool hotplug() const { return m_hotplug; }
    void setHotplug(bool on) { if (on != m_hotplug) { m_hotplug = on; Q_EMIT hotplugChanged(); } }
    QString restoreTargetId() const { return QString::fromStdString(m_sources.target()); }
    Q_INVOKABLE void setRestoreTarget(const QString& sourceId);   // "" clears the red mark
    // The selected partition's OS mount, if any, is unmounted through the platform.
    Q_INVOKABLE void unmountSelected();
    Q_INVOKABLE void refreshMounts();
    // Starts a second copy of the app with root / Administrator rights through the
    // OS's own prompt (macOS: administrator authorisation; Linux: pkexec; Windows:
    // UAC) and quits this one once the prompt was accepted. Open images are passed
    // along, as are the palette and scheme, so the elevated copy looks the same.
    Q_INVOKABLE void relaunchElevated();
    Q_INVOKABLE bool canRelaunchElevated() const;
    // macOS: opens System Settings at Privacy & Security > Full Disk Access, and
    // reveals the app bundle in Finder so it can be dragged into the list.
    Q_INVOKABLE bool canOpenPrivacySettings() const;
    Q_INVOKABLE void openPrivacySettings();
    Q_INVOKABLE void revealAppInFinder();

    // C++ side, for the other facades (GUI thread only).
    const core::OpenedSource* current() const { return m_current ? &*m_current : nullptr; }
    const stein::probe::Node* tree() const { return m_current ? &m_current->tree : nullptr; }
    const core::NodePath& selectedPath() const { return m_selectedPath; }
    const core::SourceDescriptor* descriptor(const std::string& id) const { return m_sources.find(id); }
    std::vector<std::string> passphrasesFor(const std::string& id) const;
    core::MountManager& mounts() { return m_mounts; }
    void reportError(const stein::Error& e) { Q_EMIT error(errorToVariantPublic(e)); }
    static QVariantMap errorToVariantPublic(const stein::Error& e);

Q_SIGNALS:
    void error(QVariantMap error);
    void viewChanged();
    void busyChanged();
    void currentChanged();
    void selectionChanged();
    void opened(QString id);
    void probed();   // after reprobe()
    void passphraseNeeded(QString what);
    void uiLockedChanged();
    void hotplugChanged();
    void restoreTargetChanged();
    void mountsChanged();

private:
    explicit Workspace(QObject* parent = nullptr);
    void openSourceAsync(const std::string& id, bool silentOnError);
    void setCurrent(std::optional<core::OpenedSource> opened, QVariantMap error, const std::string& id);
    void rebuildRows();
    void updateDetails();
    void setBusy(bool b);

    SourceListModel m_sources;
    TopologyModel m_topology;
    NodeDetailsObject m_details;
    std::optional<core::OpenedSource> m_current;
    std::string m_currentId, m_selectedSourceId;
    QVariantMap m_currentError;
    QVariantList m_segments;
    std::vector<core::Segment> m_segmentData;
    core::NodePath m_selectedPath;
    int m_selectedMetadata = -1;
    std::map<std::string, std::vector<std::string>> m_passphrases;
    std::vector<core::SourceDescriptor> m_images;   // opened image files, kept across refresh()
    core::MountManager m_mounts;
    std::vector<core::OsMount> m_osMounts;          // the OS mount table for the open disk
    QString m_view = "topology";
    bool m_busy = false;
    bool m_hotplug = false;
};

} // namespace drstein::ui
