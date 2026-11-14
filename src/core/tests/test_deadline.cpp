// SPDX-License-Identifier: MIT
#include "drstein/core/deadline.hpp"

#include <doctest.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>

using namespace drstein::core;
using namespace std::chrono_literals;
using Outcome = DeadlineRunner::Outcome;

namespace {

constexpr auto kPatience = 5s;   // for what must happen promptly; only a broken runner waits this long

// Holds a call until the test lets it go, and tells when it entered and when it left.
struct Gate {
    std::mutex mutex;
    std::condition_variable changed;
    bool entered = false, open = false, left = false;

    void pass() {
        std::unique_lock lock(mutex);
        entered = true;
        changed.notify_all();
        changed.wait(lock, [&] { return open; });
    }
    void leave() {
        std::lock_guard lock(mutex);
        left = true;
        changed.notify_all();
    }
    void release() {
        std::lock_guard lock(mutex);
        open = true;
        changed.notify_all();
    }
    bool waitEntered() {
        std::unique_lock lock(mutex);
        return changed.wait_for(lock, kPatience, [&] { return entered; });
    }
    bool waitLeft() {
        std::unique_lock lock(mutex);
        return changed.wait_for(lock, kPatience, [&] { return left; });
    }
};

// A call held at its gate until released; then it leaves.
std::function<void()> heldCall(const std::shared_ptr<Gate>& gate) {
    return [gate] {
        gate->pass();
        gate->leave();
    };
}

template <class Pred> bool eventually(Pred pred) {
    const auto until = std::chrono::steady_clock::now() + kPatience;
    while (!pred()) {
        if (std::chrono::steady_clock::now() > until) return false;
        std::this_thread::sleep_for(1ms);
    }
    return true;
}

} // namespace

TEST_CASE("a call that returns in time is finished, and what it made can be read") {
    DeadlineRunner runner(2);
    auto result = std::make_shared<int>(0);
    CHECK(runner.run([result] { *result = 42; }, 1s) == Outcome::Finished);
    CHECK(*result == 42);
    CHECK(runner.waitUntilIdle(kPatience));
    CHECK(runner.running() == 0);
}

TEST_CASE("a late call is given up at the deadline and runs on to its end") {
    DeadlineRunner runner(2);
    auto gate = std::make_shared<Gate>();
    auto token = std::make_shared<int>(7);
    std::weak_ptr<int> captured = token;
    const auto started = std::chrono::steady_clock::now();
    CHECK(runner.run([gate, token = std::move(token)] {
        gate->pass();
        gate->leave();
    }, 50ms) == Outcome::TimedOut);
    const auto waited = std::chrono::steady_clock::now() - started;
    CHECK(waited >= 50ms);
    CHECK(waited < kPatience);
    CHECK(gate->waitEntered());
    CHECK(runner.running() == 1);
    CHECK(!runner.waitUntilIdle(20ms));
    CHECK(!captured.expired());   // the abandoned call still holds what it captured

    gate->release();
    CHECK(gate->waitLeft());
    CHECK(runner.waitUntilIdle(kPatience));
    CHECK(runner.running() == 0);
    CHECK(captured.expired());    // and lets it go before it gives its place back
}

TEST_CASE("abandoned calls hold their places: past the bound a call waits, and is busy when none comes free") {
    DeadlineRunner runner(1);
    auto first = std::make_shared<Gate>();
    REQUIRE(runner.run(heldCall(first), 20ms) == Outcome::TimedOut);

    std::atomic<bool> secondStarted{false};
    const auto started = std::chrono::steady_clock::now();
    CHECK(runner.run([&secondStarted] { secondStarted = true; }, 50ms) == Outcome::Busy);
    CHECK(std::chrono::steady_clock::now() - started >= 50ms);
    CHECK(!secondStarted);   // never started

    // A place that comes free within the deadline is taken.
    std::thread releaser([first] {
        std::this_thread::sleep_for(30ms);
        first->release();
    });
    std::atomic<bool> thirdRan{false};
    CHECK(runner.run([&thirdRan] { thirdRan = true; }, kPatience) == Outcome::Finished);
    CHECK(thirdRan);
    releaser.join();
    CHECK(runner.waitUntilIdle(kPatience));
}

TEST_CASE("an exception out of a call in time reaches the caller; one out of an abandoned call is dropped") {
    DeadlineRunner runner(2);
    CHECK_THROWS_AS(runner.run([] { throw std::runtime_error("decoder failed"); }, 1s), std::runtime_error);
    CHECK(runner.waitUntilIdle(kPatience));

    auto gate = std::make_shared<Gate>();
    CHECK(runner.run([gate] {
        gate->pass();
        gate->leave();
        throw std::runtime_error("too late to matter");
    }, 20ms) == Outcome::TimedOut);
    gate->release();
    CHECK(runner.waitUntilIdle(kPatience));
    CHECK(runner.running() == 0);
}

TEST_CASE("the runner may go before the calls it abandoned") {
    auto runner = std::make_unique<DeadlineRunner>(1);
    auto gate = std::make_shared<Gate>();
    auto token = std::make_shared<int>(1);
    std::weak_ptr<int> captured = token;
    REQUIRE(runner->run([gate, token = std::move(token)] {
        gate->pass();
        gate->leave();
    }, 20ms) == Outcome::TimedOut);
    runner.reset();   // does not wait for the call
    CHECK(!captured.expired());
    gate->release();
    CHECK(gate->waitLeft());
    CHECK(eventually([&] { return captured.expired(); }));
}

TEST_CASE("a runner allows at least one call") {
    DeadlineRunner runner(0);
    bool ran = false;
    CHECK(runner.run([&ran] { ran = true; }, 1s) == Outcome::Finished);
    CHECK(ran);
}
