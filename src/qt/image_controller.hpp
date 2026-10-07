// SPDX-License-Identifier: MIT
// The Image view's facade: create / restore / verify / keys over the current
// source, with the form validated by the core on every change.
#pragma once

#include "drstein/core/imaging.hpp"

#include <QObject>
#include <QQmlEngine>
#include <QStringList>
#include <QUrl>
#include <QVariantList>

namespace drstein::ui {

class ImageController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString mode READ mode WRITE setMode NOTIFY modeChanged)   // create | restore | verify | keys
    // Source (what the sidebar has selected).
    Q_PROPERTY(bool hasSource READ hasSource NOTIFY sourceChanged)
    Q_PROPERTY(bool sourceIsImage READ sourceIsImage NOTIFY sourceChanged)
    Q_PROPERTY(bool sourceIsStein READ sourceIsStein NOTIFY sourceChanged)
    Q_PROPERTY(bool sourceEncrypted READ sourceEncrypted NOTIFY sourceChanged)
    Q_PROPERTY(QString sourceTitle READ sourceTitle NOTIFY sourceChanged)
    Q_PROPERTY(QString sourcePath READ sourcePath NOTIFY sourceChanged)
    Q_PROPERTY(QString sourceSizeText READ sourceSizeText NOTIFY sourceChanged)
    Q_PROPERTY(QString usedBlocksNote READ usedBlocksNote NOTIFY sourceChanged)
    Q_PROPERTY(QVariantList sourceScopes READ sourceScopes NOTIFY sourceChanged)      // whole device, then each partition
    Q_PROPERTY(int sourceScope READ sourceScope WRITE setSourceScope NOTIFY formChanged)
    Q_PROPERTY(bool partitionScope READ partitionScope NOTIFY formChanged)
    // Create form.
    Q_PROPERTY(QString destination READ destination WRITE setDestination NOTIFY formChanged)
    Q_PROPERTY(bool raw READ raw WRITE setRaw NOTIFY formChanged)
    Q_PROPERTY(QString compression READ compression WRITE setCompression NOTIFY formChanged)   // lz4 | none
    Q_PROPERTY(QString chunkSizeText READ chunkSizeText WRITE setChunkSizeText NOTIFY formChanged)
    Q_PROPERTY(QString splitSizeText READ splitSizeText WRITE setSplitSizeText NOTIFY formChanged)
    Q_PROPERTY(bool zeroFillBadSectors READ zeroFillBadSectors WRITE setZeroFillBadSectors NOTIFY formChanged)
    Q_PROPERTY(bool usedBlocksOnly READ usedBlocksOnly WRITE setUsedBlocksOnly NOTIFY formChanged)
    Q_PROPERTY(bool verifyAfter READ verifyAfter WRITE setVerifyAfter NOTIFY formChanged)
    Q_PROPERTY(bool encrypt READ encrypt WRITE setEncrypt NOTIFY formChanged)
    Q_PROPERTY(QString passphrase READ passphrase WRITE setPassphrase NOTIFY formChanged)
    Q_PROPERTY(QString notes READ notes WRITE setNotes NOTIFY formChanged)
    Q_PROPERTY(bool canStart READ canStart NOTIFY formChanged)
    Q_PROPERTY(QString validationMessage READ validationMessage NOTIFY formChanged)
    Q_PROPERTY(QStringList warnings READ warnings NOTIFY formChanged)
    Q_PROPERTY(QString planText READ planText NOTIFY formChanged)
    // Restore form.
    Q_PROPERTY(QVariantList restoreTargets READ restoreTargets NOTIFY formChanged)
    Q_PROPERTY(QString restoreScopeText READ restoreScopeText NOTIFY formChanged)       // "whole-device image · 139.5 MB" / provenance
    Q_PROPERTY(bool restorePartitionImage READ restorePartitionImage NOTIFY formChanged)
    Q_PROPERTY(bool restoreEncrypted READ restoreEncrypted NOTIFY formChanged)
    Q_PROPERTY(bool restoreUnlocked READ restoreUnlocked NOTIFY formChanged)
    Q_PROPERTY(QString restoreTargetId READ restoreTargetId WRITE setRestoreTargetId NOTIFY formChanged)
    Q_PROPERTY(QVariantMap restoreTarget READ restoreTarget NOTIFY formChanged)
    Q_PROPERTY(bool restoreVerifyFirst READ restoreVerifyFirst WRITE setRestoreVerifyFirst NOTIFY formChanged)
    Q_PROPERTY(bool restoreAllowSmaller READ restoreAllowSmaller WRITE setRestoreAllowSmaller NOTIFY formChanged)
    Q_PROPERTY(QString restoreZeros READ restoreZeros WRITE setRestoreZeros NOTIFY formChanged)   // "write" | "gaps" | "skip"
    Q_PROPERTY(QString restoreZeroPlanText READ restoreZeroPlanText NOTIFY formChanged)        // computed from the chunk map
    Q_PROPERTY(bool restoreZerosDefaulted READ restoreZerosDefaulted NOTIFY formChanged)       // the mode came from the target kind, not from the user
    Q_PROPERTY(QString restoreImagePath READ restoreImagePath WRITE setRestoreImagePath NOTIFY formChanged)
    Q_PROPERTY(bool canRestore READ canRestore NOTIFY formChanged)
    Q_PROPERTY(QString restoreMessage READ restoreMessage NOTIFY formChanged)
    // Verify.
    Q_PROPERTY(int verifyLevel READ verifyLevel WRITE setVerifyLevel NOTIFY formChanged)
    Q_PROPERTY(bool canVerify READ canVerify NOTIFY formChanged)
    // Keys.
    Q_PROPERTY(QVariantList keys READ keys NOTIFY keysChanged)
    Q_PROPERTY(QString keysMessage READ keysMessage NOTIFY keysChanged)

public:
    explicit ImageController(QObject* parent = nullptr);

    QString mode() const { return m_mode; }
    void setMode(const QString& m);
    bool hasSource() const;
    bool sourceIsImage() const;
    bool sourceIsStein() const;
    bool sourceEncrypted() const;
    QString sourceTitle() const;
    QString sourcePath() const;
    QString sourceSizeText() const;
    QString usedBlocksNote() const;
    QVariantList sourceScopes() const;
    int sourceScope() const { return m_scopeIndex; }
    void setSourceScope(int i);
    bool partitionScope() const { return m_plan && m_plan->partition; }

    QString destination() const { return m_form.destination.empty() ? QString() : QString::fromStdString(m_form.destination.string()); }
    void setDestination(const QString& d);
    bool raw() const { return m_form.raw; }
    void setRaw(bool on);
    QString compression() const;
    void setCompression(const QString& c);
    QString chunkSizeText() const { return QString::fromStdString(m_form.chunkSizeText); }
    void setChunkSizeText(const QString& t);
    QString splitSizeText() const { return QString::fromStdString(m_form.splitSizeText); }
    void setSplitSizeText(const QString& t);
    bool zeroFillBadSectors() const;
    void setZeroFillBadSectors(bool on);
    bool usedBlocksOnly() const { return m_form.usedBlocksOnly; }
    void setUsedBlocksOnly(bool on);
    bool verifyAfter() const { return m_form.verifyAfter; }
    void setVerifyAfter(bool on);
    bool encrypt() const { return m_form.encrypt; }
    void setEncrypt(bool on);
    QString passphrase() const { return QString::fromStdString(m_form.passphrase); }
    void setPassphrase(const QString& p);
    QString notes() const { return QString::fromStdString(m_form.notes); }
    void setNotes(const QString& n);
    bool canStart() const { return m_plan.has_value(); }
    QString validationMessage() const { return m_validation; }
    QStringList warnings() const { return m_warnings; }
    QString planText() const;

    QVariantList restoreTargets() const;
    QString restoreScopeText() const;
    bool restorePartitionImage() const { return m_restoreScope && m_restoreScope->partition; }
    bool restoreEncrypted() const { return m_restoreScope && m_restoreScope->encrypted; }
    bool restoreUnlocked() const { return !m_restoreScope || m_restoreScope->unlocked; }
    // Tries the passphrase against the image's key area; on success it is kept for the restore.
    Q_INVOKABLE bool checkRestorePassphrase(const QString& passphrase);
    QString restoreTargetId() const { return m_restoreTargetId; }
    void setRestoreTargetId(const QString& id);
    QVariantMap restoreTarget() const;
    bool restoreVerifyFirst() const { return m_restore.verifyFirst; }
    void setRestoreVerifyFirst(bool on);
    bool restoreAllowSmaller() const { return m_restore.allowSmaller; }
    void setRestoreAllowSmaller(bool on);
    QString restoreZeros() const;
    void setRestoreZeros(const QString& mode);
    QString restoreZeroPlanText() const { return m_zeroPlanText; }
    bool restoreZerosDefaulted() const { return !m_zerosExplicit; }
    QString restoreImagePath() const { return QString::fromStdString(m_restore.image.string()); }
    void setRestoreImagePath(const QString& p);
    bool canRestore() const;
    QString restoreMessage() const;

    int verifyLevel() const { return m_verifyLevel; }
    void setVerifyLevel(int level);
    bool canVerify() const;

    QVariantList keys() const { return m_keys; }
    QString keysMessage() const { return m_keysMessage; }

    Q_INVOKABLE QString defaultDestination() const;
    Q_INVOKABLE void setDestinationUrl(const QUrl& url);
    Q_INVOKABLE void setRestoreImageUrl(const QUrl& url);
    Q_INVOKABLE void start();
    Q_INVOKABLE void restore(const QString& passphrase);
    Q_INVOKABLE void verify(const QString& passphrase);
    Q_INVOKABLE void reloadKeys();
    Q_INVOKABLE QVariantMap addKey(const QString& current, const QString& fresh, const QString& label);
    Q_INVOKABLE QVariantMap removeKey(const QString& current, int id);

Q_SIGNALS:
    void modeChanged();
    void sourceChanged();
    void formChanged();
    void keysChanged();

private:
    void revalidate();
    void onSourceChanged();
    void reloadRestoreScope();

    QString m_mode = "create";
    core::CreateImageForm m_form;
    std::optional<core::CreatePlan> m_plan;
    QString m_validation;
    QStringList m_warnings;
    core::RestoreForm m_restore;
    QString m_restoreTargetId;             // "<source id>" for a device, "<source id>#<partition index>" for a partition
    std::optional<core::RestoreScope> m_restoreScope;
    QString m_restoreScopeError;
    QString m_zeroPlanText;
    bool m_zerosExplicit = false;          // the user picked a zero mode for this image; targets no longer change it
    int m_scopeIndex = 0;
    int m_verifyLevel = 3;
    QVariantList m_keys;
    QString m_keysMessage;
};

} // namespace drstein::ui
