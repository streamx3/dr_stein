// SPDX-License-Identifier: MIT
// ProgressRelay: a thread-safe ProgressSink that forwards snapshots and
// messages to std::function handlers. The Qt layer installs handlers that
// post to the GUI thread; tests install lambdas. Owns the CancelToken of the
// job in flight (a fresh one per reset(), since tokens cannot be cleared).
#pragma once

#include "stein/core/progress.hpp"
#include "stein/core/report.hpp"

#include <functional>
#include <memory>
#include <mutex>
#include <string>

namespace drstein::core {

class ProgressRelay final : public stein::ProgressSink {
public:
    using OnProgress = std::function<void(const stein::ProgressSnapshot&)>;
    using OnMessage = std::function<void(std::string)>;

    ProgressRelay();

    void onProgress(const stein::ProgressSnapshot& snapshot) override;
    void onMessage(std::string_view message) override;

    void setHandlers(OnProgress onProgress, OnMessage onMessage);
    // The token the next Progress should be built with. Valid until reset().
    stein::CancelToken& cancelToken() { return *m_cancel; }
    void cancel() { m_cancel->cancel(); }
    bool isCancelled() const { return m_cancel->isCancelled(); }
    // New token, cleared snapshot. Call before starting a job, never during one.
    void reset();

    stein::ProgressSnapshot last() const;

private:
    mutable std::mutex m_mutex;
    OnProgress m_onProgress;
    OnMessage m_onMessage;
    std::unique_ptr<stein::CancelToken> m_cancel;
    stein::ProgressSnapshot m_last;
};

// Copies a finished Report tree (title, status, details, lines, children) under `dst`.
// Durations are not carried over (they are private to the source). Used to show the
// library's own reports (scenarios, operation stacks) inside the job's report.
void copyReportInto(stein::Report& dst, const stein::Report& src);

} // namespace drstein::core
