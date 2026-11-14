// SPDX-License-Identifier: MIT
// Calls that cannot be interrupted and may not return in time: a library's
// decode of a hostile file. Each runs on a thread of its own; the caller waits
// for it up to a deadline and, past it, goes on without the result. A late
// call is abandoned, not stopped: it runs to its end on its own and keeps a
// place until then, so a few such calls cannot pile up into many.
#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>

namespace drstein::core {

class DeadlineRunner {
public:
    enum class Outcome : std::uint8_t {
        Finished,   // the call returned within the deadline
        TimedOut,   // it is still running: abandoned, it runs on to its end
        Busy,       // no place came free within the deadline: the call never started
    };

    // At most `places` calls run at once, the abandoned ones included (at least one).
    explicit DeadlineRunner(std::size_t places);
    // Never waits for a call: an abandoned one keeps what it needs alive and winds down on its own.
    ~DeadlineRunner();
    DeadlineRunner(const DeadlineRunner&) = delete;
    DeadlineRunner& operator=(const DeadlineRunner&) = delete;

    // Runs `call` on a thread of its own and waits for it at most `deadline`, counted from this
    // call: waiting for a place takes from it. `call` may outlive the caller, so it must own all
    // it touches (captures by value or shared_ptr) and hand its result over through something it
    // co-owns; the caller may read that only after Finished. `call` is destroyed on its thread,
    // before the place is given back. An exception out of a call that finished in time is
    // rethrown here; one out of an abandoned call is dropped. Throws std::system_error when no
    // thread can be had.
    Outcome run(std::function<void()> call, std::chrono::milliseconds deadline);

    // Calls running now, the abandoned ones included.
    std::size_t running() const;
    // Waits until none runs, at most `patience`; false when some still do. For a process about
    // to run its static destructors: a call may be inside a library they take down.
    bool waitUntilIdle(std::chrono::milliseconds patience) const;

private:
    struct State;
    std::shared_ptr<State> m_state;
};

} // namespace drstein::core
