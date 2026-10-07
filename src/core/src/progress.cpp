// SPDX-License-Identifier: MIT
#include "drstein/core/progress.hpp"

namespace drstein::core {

ProgressRelay::ProgressRelay() : m_cancel(std::make_unique<stein::CancelToken>()) {}

void ProgressRelay::onProgress(const stein::ProgressSnapshot& snapshot) {
    OnProgress handler;
    {
        std::lock_guard lock(m_mutex);
        m_last = snapshot;
        handler = m_onProgress;
    }
    if (handler) handler(snapshot);
}

void ProgressRelay::onMessage(std::string_view message) {
    OnMessage handler;
    {
        std::lock_guard lock(m_mutex);
        handler = m_onMessage;
    }
    if (handler) handler(std::string(message));
}

void ProgressRelay::setHandlers(OnProgress onProgress, OnMessage onMessage) {
    std::lock_guard lock(m_mutex);
    m_onProgress = std::move(onProgress);
    m_onMessage = std::move(onMessage);
}

void ProgressRelay::reset() {
    std::lock_guard lock(m_mutex);
    m_cancel = std::make_unique<stein::CancelToken>();
    m_last = stein::ProgressSnapshot{};
}

stein::ProgressSnapshot ProgressRelay::last() const {
    std::lock_guard lock(m_mutex);
    return m_last;
}

void copyReportInto(stein::Report& dst, const stein::Report& src) {
    stein::Report& child = dst.addChild(src.title());
    for (const auto& [k, v] : src.details()) child.addDetail(k, v);
    for (const auto& l : src.lines()) child.addLine(l);
    for (const auto& c : src.children()) copyReportInto(child, *c);
    child.setStatus(src.status());
}

} // namespace drstein::core
