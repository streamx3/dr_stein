// SPDX-License-Identifier: MIT
#include "drstein/core/preview_loader.hpp"

#include <atomic>
#include <condition_variable>
#include <exception>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>

namespace drstein::core {

using namespace stein;

namespace {

struct Workers {
    std::mutex mutex;
    std::condition_variable gone;
    std::size_t alive = 0;
};

// Never destroyed: a worker may count itself out while, or after, the static destructors run.
Workers& workers() {
    static auto* w = new Workers;
    return *w;
}

Error cancelledError() { return Error(ErrorCategory::Cancelled, "preview cancelled"); }

Preview unreadable(std::string note) {
    Preview p;
    p.kind = PreviewKind::Unreadable;
    p.note = std::move(note);
    return p;
}

} // namespace

// Owned by the loader and by its worker alike: a worker that is in the middle of a read or a
// transform when the loader goes away finishes that call on its own, and takes the reader down
// with it afterwards.
struct PreviewLoader::State {
    struct Job {
        std::uint64_t ticket = 0;
        PreviewTarget target;
    };

    mutable std::mutex mutex;             // everything down to `deliver`; held while delivering
    std::condition_variable wake;
    bool stop = false;
    std::uint64_t issued = 0;             // the last ticket handed out
    std::uint64_t wanted = 0;             // the one ticket that may still be delivered; 0 = none
    std::optional<Job> waiting;
    std::uint64_t serving = 0;            // the request the worker is in; 0 = idle
    std::atomic<PreviewActivity::Phase> phase{PreviewActivity::Phase::Idle};   // of `serving`
    std::atomic<bool> cancelled{false};   // tells the request being served to stop at its next chunk
    Deliver deliver;

    std::unique_ptr<fs::Reader> reader;   // the worker's alone
    PreviewLimits limits;
    Transform transform;

    void supersede() {
        wanted = 0;
        waiting.reset();
        if (serving) cancelled.store(true, std::memory_order_relaxed);
    }

    // The target, found again in this reader by its path, previewed. Anything that keeps it from
    // being found is the same to the user as a read that failed.
    Expected<Preview> previewOf(const PreviewTarget& target) {
        if (target.path.empty()) return unreadable("no file was named");
        Expected<fs::Inode> at = reader->root();
        for (const std::string& name : target.path) {
            if (cancelled.load(std::memory_order_relaxed)) return fail(cancelledError());
            if (!at) break;
            at = reader->lookup(*at, name);
        }
        if (!at) return unreadable(at.error().message());
        auto st = reader->stat(*at);
        if (!st) return unreadable(st.error().message());
        if (st->type != target.st.type || st->size != target.st.size || st->mtime != target.st.mtime)
            return unreadable("not the file listed under this name: another entry has the same name, or it changed");
        return preview(*reader, *at, *st, limits, &cancelled);
    }

    PreviewResult serve(const Job& job) {
        PreviewResult result{job.ticket, fail(ErrorCategory::InvalidArgument, "no reader"), nullptr};
        // A thread has nobody to throw to: what escapes a reader or the transform becomes the result.
        try {
            if (reader) result.preview = previewOf(job.target);
            if (result.preview && transform && !cancelled.load(std::memory_order_relaxed)) {
                phase.store(PreviewActivity::Phase::Transforming, std::memory_order_relaxed);
                result.decoded = transform(*result.preview);
            }
        } catch (const std::exception& e) {
            result.preview = fail(ErrorCategory::Internal, e.what());
            result.decoded.reset();
        } catch (...) {
            result.preview = fail(ErrorCategory::Internal, "the preview failed");
            result.decoded.reset();
        }
        return result;
    }

    void work() {
        std::unique_lock lock(mutex);
        for (;;) {
            wake.wait(lock, [&] { return stop || waiting; });
            if (stop) return;
            const Job job = std::move(*std::exchange(waiting, std::nullopt));
            serving = job.ticket;
            phase.store(PreviewActivity::Phase::Reading, std::memory_order_relaxed);
            cancelled.store(false, std::memory_order_relaxed);
            lock.unlock();
            PreviewResult result = serve(job);
            lock.lock();
            serving = 0;
            phase.store(PreviewActivity::Phase::Idle, std::memory_order_relaxed);
            const bool gaveUp = !result.preview && result.preview.error().category() == ErrorCategory::Cancelled;
            if (!stop && wanted == job.ticket && !gaveUp && deliver) {
                deliver(std::move(result));
                continue;
            }
            // A superseded picture can be tens of megabytes: free it without holding up request().
            lock.unlock();
            result = PreviewResult{};
            lock.lock();
        }
    }
};

PreviewLoader::PreviewLoader(std::unique_ptr<fs::Reader> reader, PreviewLimits limits, Transform transform, Deliver deliver) : m_state(std::make_shared<State>()) {
    m_state->reader = std::move(reader);
    m_state->limits = limits;
    m_state->transform = std::move(transform);
    m_state->deliver = std::move(deliver);
    Workers& all = workers();
    {
        std::lock_guard lock(all.mutex);
        ++all.alive;
    }
    // Detached from the start: the destructor must not wait for a read on failing media, so
    // there is never a join, and the state the worker co-owns is all it needs to wind down.
    try {
        std::thread([state = m_state]() mutable {
            state->work();
            state.reset();   // the reader goes before the worker counts itself out
            Workers& w = workers();
            {
                std::lock_guard lock(w.mutex);
                --w.alive;
            }
            w.gone.notify_all();
        }).detach();
    } catch (...) {
        std::lock_guard lock(all.mutex);
        --all.alive;
        throw;
    }
}

PreviewLoader::~PreviewLoader() {
    {
        std::lock_guard lock(m_state->mutex);
        m_state->stop = true;
        m_state->supersede();
        m_state->deliver = nullptr;
    }
    m_state->wake.notify_all();
}

std::uint64_t PreviewLoader::request(PreviewTarget target) {
    std::uint64_t ticket = 0;
    {
        std::lock_guard lock(m_state->mutex);
        m_state->supersede();
        ticket = m_state->wanted = ++m_state->issued;
        m_state->waiting = State::Job{ticket, std::move(target)};
    }
    m_state->wake.notify_one();
    return ticket;
}

void PreviewLoader::cancel() {
    std::lock_guard lock(m_state->mutex);
    m_state->supersede();
}

PreviewActivity PreviewLoader::activity() const {
    std::lock_guard lock(m_state->mutex);
    if (!m_state->serving) return {};
    return {m_state->serving, m_state->phase.load(std::memory_order_relaxed)};
}

std::size_t PreviewLoader::workersAlive() {
    Workers& all = workers();
    std::lock_guard lock(all.mutex);
    return all.alive;
}

bool PreviewLoader::waitForWorkers(std::chrono::milliseconds patience) {
    Workers& all = workers();
    std::unique_lock lock(all.mutex);
    return all.gone.wait_for(lock, patience, [&] { return all.alive == 0; });
}

} // namespace drstein::core
