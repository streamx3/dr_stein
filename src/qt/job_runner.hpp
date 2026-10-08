// SPDX-License-Identifier: MIT
// One long operation at a time on a worker thread, with the library's
// Progress relayed to the GUI thread and the Report shown when it ends.
// The library's objects are not thread-safe, so while a job runs the UI
// keeps its hands off the device: every facade checks JobRunner::running().
#pragma once

#include "drstein/core/progress.hpp"
#include "report_model.hpp"
#include "stein/core/error.hpp"
#include "stein/core/report.hpp"

#include <QObject>
#include <QQmlEngine>
#include <QStringList>
#include <QThread>

#include <functional>
#include <memory>

namespace drstein::ui {

class JobRunner : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    Q_PROPERTY(QString title READ title NOTIFY runningChanged)
    Q_PROPERTY(QString kind READ kind NOTIFY runningChanged)
    Q_PROPERTY(QString phase READ phase NOTIFY progressChanged)
    Q_PROPERTY(double fraction READ fraction NOTIFY progressChanged)
    Q_PROPERTY(bool indeterminate READ indeterminate NOTIFY progressChanged)
    Q_PROPERTY(QString percentText READ percentText NOTIFY progressChanged)
    Q_PROPERTY(QString doneText READ doneText NOTIFY progressChanged)
    Q_PROPERTY(QString totalText READ totalText NOTIFY progressChanged)
    Q_PROPERTY(QString rateText READ rateText NOTIFY progressChanged)
    Q_PROPERTY(QString etaText READ etaText NOTIFY progressChanged)
    Q_PROPERTY(QStringList messages READ messages NOTIFY messagesChanged)
    Q_PROPERTY(drstein::ui::ReportModel* report READ report CONSTANT)
    Q_PROPERTY(bool hasResult READ hasResult NOTIFY finished)
    Q_PROPERTY(bool lastOk READ lastOk NOTIFY finished)
    Q_PROPERTY(QString lastTitle READ lastTitle NOTIFY finished)
    Q_PROPERTY(QString lastKind READ lastKind NOTIFY finished)
    Q_PROPERTY(QVariantMap lastError READ lastError NOTIFY finished)
    Q_PROPERTY(QString lastSummary READ lastSummary NOTIFY finished)

public:
    using JobFn = std::function<stein::Expected<void>(stein::Progress&, stein::Report&)>;
    using DoneFn = std::function<void(bool ok, const stein::Error& error)>;

    static JobRunner* instance();
    static JobRunner* create(QQmlEngine*, QJSEngine*);
    ~JobRunner() override;

    // `kind` tags which view owns the job card ("image", "profile", "tools", "partitions", "browse", "probe").
    // Returns false (and emits error) when a job is already running.
    bool start(const QString& title, const QString& kind, JobFn fn, DoneFn done = {});
    // A short line the card shows after the job ("restored and verified"), set by the done callback.
    void setSummary(const QString& text);

    bool running() const { return m_running; }
    QString title() const { return m_title; }
    QString kind() const { return m_kind; }
    QString phase() const;
    double fraction() const;
    bool indeterminate() const;
    QString percentText() const;
    QString doneText() const;
    QString totalText() const;
    QString rateText() const;
    QString etaText() const;
    QStringList messages() const { return m_messages; }
    ReportModel* report() { return &m_reportModel; }
    bool hasResult() const { return m_hasResult; }
    bool lastOk() const { return m_lastOk; }
    QString lastTitle() const { return m_lastTitle; }
    QString lastKind() const { return m_lastKind; }
    QVariantMap lastError() const { return m_lastError; }
    QString lastSummary() const { return m_lastSummary; }

    Q_INVOKABLE void cancel();
    Q_INVOKABLE void clearResult();

Q_SIGNALS:
    void runningChanged();
    void progressChanged();
    void messagesChanged();
    void finished(bool ok);
    void error(QVariantMap error);

private:
    explicit JobRunner(QObject* parent = nullptr);
    void onProgress(const stein::ProgressSnapshot& snapshot);   // GUI thread
    void onMessage(const QString& message);                     // GUI thread
    void onDone(bool ok, const stein::Error& error);             // GUI thread

    core::ProgressRelay m_relay;
    stein::ProgressSnapshot m_snapshot;
    std::unique_ptr<stein::Report> m_report;
    ReportModel m_reportModel;
    QThread* m_thread = nullptr;
    bool m_running = false;
    QString m_title, m_kind;
    QStringList m_messages;
    DoneFn m_done;
    bool m_hasResult = false, m_lastOk = false;
    QString m_lastTitle, m_lastKind, m_lastSummary;
    QVariantMap m_lastError;
};

} // namespace drstein::ui
