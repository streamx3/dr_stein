// SPDX-License-Identifier: MIT
// The Profiles view: cards over core::ProfileStore, each with three jobs.
#pragma once

#include "drstein/core/profiles.hpp"

#include <QAbstractListModel>
#include <QQmlEngine>
#include <QUrl>

#include <vector>

namespace drstein::ui {

class ProfilesModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(QString directory READ directory NOTIFY countChanged)
    Q_PROPERTY(bool canCreateFromCurrent READ canCreateFromCurrent NOTIFY countChanged)
    Q_PROPERTY(QString createHint READ createHint NOTIFY countChanged)

public:
    enum Roles {
        Name = Qt::UserRole + 1, Kicker, Description, DiskText, DiskPresent, SelectorText, ImageText, ImagePresent, MadeText, FitsText,
        PolicyText, VerifyLevelText, CanBackup, CanRestore, CanVerify, Warnings, File, ImagePath, Encrypted, RequiresElevation
    };
    static ProfilesModel* instance();
    static ProfilesModel* create(QQmlEngine*, QJSEngine*);

    int rowCount(const QModelIndex& = {}) const override { return static_cast<int>(m_cards.size()); }
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    QString directory() const;
    bool canCreateFromCurrent() const;
    QString createHint() const;

    Q_INVOKABLE void reload();
    Q_INVOKABLE void run(int row, const QString& scenario, bool dryRun, const QString& passphrase);
    Q_INVOKABLE QVariantMap newFromCurrent(const QUrl& imageFile, const QString& name, const QString& description, bool usedOnly, bool encrypt);
    Q_INVOKABLE QVariantMap remove(int row);
    Q_INVOKABLE QVariantMap get(int row) const;

Q_SIGNALS:
    void countChanged();

private:
    explicit ProfilesModel(QObject* parent = nullptr);
    std::vector<core::ProfileCard> m_cards;
};

} // namespace drstein::ui
