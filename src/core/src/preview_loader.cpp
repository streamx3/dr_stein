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

// Owned by the loader and by its worker alike: a worker that is in the middle of a read or a
// transform when the loader goes away finishes that call on its own, and takes the reader down
// with it afterwards.
struct PreviewLoader::State {
    struct Job {
        std::uint64_t ticket = 0;
        fs::Inode file;
        fs::Stat st;
    };

    std::mutex mutex;                     // everything down to `deliver`; held while delivering
    std::condition_variable wake;
    bool stop = false;
    std::uint64_t issued = 0;             // the last ticket handed out
    std::uint64_t wanted = 0;             // the one ticket that may still be delivered; 0 = none
    std::optional<Job> waiting;
    bool busy = false;                    // the worker is serving a request
    std::atomic<bool> cancelled{false};   // tells that request to stop at its next chunk
    Deliver deliver;

    std::unique_ptr<fs::Reader> reader;   // the worker's alone
    PreviewLimits limits;
    Transform transform;

    void supersede() {
        wanted = 0;
        waiting.reset();
        if (busy) cancelled.store(true, std::memory_order_relaxed);
    }

    PreviewResult serve(const Job& job) {
        PreviewResult result{job.ticket, fail(ErrorCategory::InvalidArgument, "no reader"), nullptr};
        // A thread has nobody to throw to: what escapes a reader or the transform becomes the result.
        try {
            if (reader) result.preview = preview(*reader, job.file, job.st, limits, &cancelled);
            if (result.preview && transform && !cancelled.load(std::memory_order_relaxed)) result.decoded = transform(*result.preview);
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
            const Job job = *std::exchange(waiting, std::nullopt);
            busy = true;
            cancelled.store(false, std::memory_order_relaxed);
            lock.unlock();
            PreviewResult result = serve(job);
            lock.lock();
            busy = false;
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
    // Detached from the start: the destructor must not wait for a read on failing media, so
    // there is never a join, and the state the worker co-owns is all it needs to wind down.
    std::thread([state = m_state] { state->work(); }).detach();
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

std::uint64_t PreviewLoader::request(const fs::Inode& file, const fs::Stat& st) {
    std::uint64_t ticket = 0;
    {
        std::lock_guard lock(m_state->mutex);
        m_state->supersede();
        ticket = m_state->wanted = ++m_state->issued;
        m_state->waiting = State::Job{ticket, file, st};
    }
    m_state->wake.notify_one();
    return ticket;
}

void PreviewLoader::cancel() {
    std::lock_guard lock(m_state->mutex);
    m_state->supersede();
}

} // namespace drstein::core
