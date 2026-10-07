// SPDX-License-Identifier: MIT
#include "job_runner.hpp"

#include "drstein/core/format.hpp"
#include "util.hpp"

#include <QMetaObject>

namespace drstein::ui {

namespace {
JobRunner* g_instance = nullptr;
}

JobRunner::JobRunner(QObject* parent) : QObject(parent) {
    m_relay.setHandlers(
        [this](const stein::ProgressSnapshot& s) {
            QMetaObject::invokeMethod(this, [this, s] { onProgress(s); }, Qt::QueuedConnection);
        },
        [this](std::string m) {
            const QString text = qs(m);
            QMetaObject::invokeMethod(this, [this, text] { onMessage(text); }, Qt::QueuedConnection);
        });
}

JobRunner::~JobRunner() {
    if (m_thread) {
        m_relay.cancel();
        m_thread->wait();
    }
}

JobRunner* JobRunner::instance() {
    if (!g_instance) g_instance = new JobRunner();
    return g_instance;
}

JobRunner* JobRunner::create(QQmlEngine*, QJSEngine*) {
    JobRunner* j = instance();
    QQmlEngine::setObjectOwnership(j, QQmlEngine::CppOwnership);
    return j;
}

bool JobRunner::start(const QString& title, const QString& kind, JobFn fn, DoneFn done) {
    if (m_running) {
        Q_EMIT error(errorToVariant(stein::Error(stein::ErrorCategory::Busy, "another operation is still running: " + ss(m_title))));
        return false;
    }
    if (m_thread) {
        m_thread->wait();
        m_thread->deleteLater();
        m_thread = nullptr;
    }
    m_relay.reset();
    m_snapshot = {};
    m_messages.clear();
    m_lastSummary.clear();
    m_report = std::make_unique<stein::Report>(ss(title));
    m_reportModel.clear();
    m_title = title;
    m_kind = kind;
    m_done = std::move(done);
    m_running = true;
    m_hasResult = false;
    Q_EMIT runningChanged();
    Q_EMIT progressChanged();
    Q_EMIT messagesChanged();

    stein::Report* report = m_report.get();
    core::ProgressRelay* relay = &m_relay;
    m_thread = QThread::create([this, fn = std::move(fn), report, relay] {
        stein::Progress progress(*relay, &relay->cancelToken());
        auto r = fn(progress, *report);
        const bool ok = r.has_value();
        const stein::Error err = ok ? stein::Error{} : r.error();
        QMetaObject::invokeMethod(this, [this, ok, err] { onDone(ok, err); }, Qt::QueuedConnection);
    });
    m_thread->setObjectName("drstein-job");
    m_thread->start();
    return true;
}

void JobRunner::setSummary(const QString& text) { m_lastSummary = text; }

void JobRunner::cancel() { m_relay.cancel(); }

void JobRunner::clearResult() {
    if (m_running) return;
    m_hasResult = false;
    m_reportModel.clear();
    m_messages.clear();
    Q_EMIT messagesChanged();
    Q_EMIT finished(m_lastOk);
}

void JobRunner::onProgress(const stein::ProgressSnapshot& s) {
    if (!m_running) return;
    m_snapshot = s;
    Q_EMIT progressChanged();
}

void JobRunner::onMessage(const QString& message) {
    if (!m_running) return;
    m_messages << message;
    if (m_messages.size() > 500) m_messages.removeFirst();
    Q_EMIT messagesChanged();
}

void JobRunner::onDone(bool ok, const stein::Error& err) {
    m_running = false;
    m_lastOk = ok;
    m_lastTitle = m_title;
    m_lastKind = m_kind;
    m_lastError = ok ? QVariantMap{} : errorToVariant(err);
    m_hasResult = true;
    if (m_report && m_report->status() == stein::ReportStatus::Pending) m_report->setStatus(ok ? stein::ReportStatus::Success : stein::ReportStatus::Error);
    if (m_report && m_report->status() == stein::ReportStatus::Running) m_report->finish(ok ? stein::ReportStatus::Success : stein::ReportStatus::Error);
    m_reportModel.setReport(m_report.get());
    DoneFn done = std::move(m_done);
    m_done = {};
    if (done) done(ok, err);
    if (!ok && err.category() != stein::ErrorCategory::Cancelled) Q_EMIT error(m_lastError);
    Q_EMIT runningChanged();
    Q_EMIT progressChanged();
    Q_EMIT finished(ok);
}

QString JobRunner::phase() const { return qs(m_snapshot.phase); }
double JobRunner::fraction() const { return m_snapshot.total ? m_snapshot.fraction() : 0.0; }
bool JobRunner::indeterminate() const { return m_running && m_snapshot.total == 0; }
QString JobRunner::percentText() const { return m_snapshot.total ? qs(core::percentText(m_snapshot.fraction())) : QString(); }

namespace {
QString unitText(const stein::ProgressSnapshot& s, std::uint64_t v) {
    if (s.unit == "bytes") return qs(core::sizeText(v));
    return QString::number(v) + " " + qs(s.unit);
}
} // namespace

QString JobRunner::doneText() const { return unitText(m_snapshot, m_snapshot.done); }
QString JobRunner::totalText() const { return m_snapshot.total ? unitText(m_snapshot, m_snapshot.total) : QString(); }
QString JobRunner::rateText() const {
    if (m_snapshot.rate <= 0) return {};
    if (m_snapshot.unit == "bytes") return qs(core::rateText(m_snapshot.rate));
    return QString::number(static_cast<long>(m_snapshot.rate)) + " " + qs(m_snapshot.unit) + "/s";
}
QString JobRunner::etaText() const { return m_snapshot.total ? qs(core::etaText(m_snapshot.etaSeconds)) : QString(); }

} // namespace drstein::ui
