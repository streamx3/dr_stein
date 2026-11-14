// SPDX-License-Identifier: MIT
#include "drstein/core/deadline.hpp"

#include <algorithm>
#include <condition_variable>
#include <exception>
#include <mutex>
#include <thread>
#include <utility>

namespace drstein::core {

// Co-owned by the runner and by every call's thread: an abandoned call gives its place back
// after the runner may be gone.
struct DeadlineRunner::State {
    mutable std::mutex mutex;
    mutable std::condition_variable changed;
    std::size_t places = 1;
    std::size_t running = 0;
};

namespace {

// One call's end, shared by its thread and the caller that waits for it.
struct Ending {
    std::mutex mutex;
    std::condition_variable done;
    bool finished = false;
    std::exception_ptr error;
};

} // namespace

DeadlineRunner::DeadlineRunner(std::size_t places) : m_state(std::make_shared<State>()) { m_state->places = std::max<std::size_t>(places, 1); }

DeadlineRunner::~DeadlineRunner() = default;

DeadlineRunner::Outcome DeadlineRunner::run(std::function<void()> call, std::chrono::milliseconds deadline) {
    const auto until = std::chrono::steady_clock::now() + deadline;
    const std::shared_ptr<State> state = m_state;
    {
        std::unique_lock lock(state->mutex);
        if (!state->changed.wait_until(lock, until, [&] { return state->running < state->places; })) return Outcome::Busy;
        ++state->running;
    }
    auto ending = std::make_shared<Ending>();
    try {
        std::thread([state, ending, call = std::move(call)]() mutable {
            std::exception_ptr error;
            try {
                call();
            } catch (...) {
                error = std::current_exception();
            }
            // What the call captured goes here, on its own thread, and before the place is given
            // back: a process that waited until idle finds none of it left.
            call = nullptr;
            {
                std::lock_guard lock(ending->mutex);
                ending->finished = true;
                ending->error = std::move(error);
            }
            ending->done.notify_all();
            {
                std::lock_guard lock(state->mutex);
                --state->running;
            }
            state->changed.notify_all();
        }).detach();
    } catch (...) {
        {
            std::lock_guard lock(state->mutex);
            --state->running;
        }
        state->changed.notify_all();
        throw;
    }
    std::unique_lock lock(ending->mutex);
    if (!ending->done.wait_until(lock, until, [&] { return ending->finished; })) return Outcome::TimedOut;
    if (ending->error) std::rethrow_exception(ending->error);
    return Outcome::Finished;
}

std::size_t DeadlineRunner::running() const {
    std::lock_guard lock(m_state->mutex);
    return m_state->running;
}

bool DeadlineRunner::waitUntilIdle(std::chrono::milliseconds patience) const {
    std::unique_lock lock(m_state->mutex);
    return m_state->changed.wait_for(lock, patience, [&] { return m_state->running == 0; });
}

} // namespace drstein::core
