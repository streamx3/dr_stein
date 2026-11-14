// SPDX-License-Identifier: MIT
// Previews off the caller's thread: one worker that owns a Reader of its own
// (a Reader is not thread-safe, so it is never the Browser's) and serves only
// the latest request, so stepping through a directory never queues up reads.
#pragma once

#include "drstein/core/browse.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace drstein::core {

struct PreviewResult {
    std::uint64_t ticket = 0;
    stein::Expected<Preview> preview;   // a read failure is a Preview of kind Unreadable; an error is anything else that went wrong
    std::shared_ptr<void> decoded;      // what the transform made of the preview; empty without one or on an error
};

// The file a request is for: the names that lead to it from the root of the loader's reader
// (Browser::pathTo), and what the requester's listing said of it. The loader finds the file
// again by its path, never by the requester's inode: two readers of one filesystem need not
// number its files alike (btrfs numbers subvolumes in the order a reader meets them). What it
// finds must still be of the listed type, size and modification time; otherwise, or when the
// path leads nowhere, the preview is of kind Unreadable.
struct PreviewTarget {
    std::vector<std::string> path;
    stein::fs::Stat st;
};

// What the worker is doing, for a caller that wonders whether it is stuck: the request it is
// in (0 when idle), superseded or not, and whether it reads or transforms.
struct PreviewActivity {
    enum class Phase : std::uint8_t { Idle, Reading, Transforming };
    std::uint64_t ticket = 0;
    Phase phase = Phase::Idle;
};

class PreviewLoader {
public:
    // Runs on the worker with no lock held, and may still be running after the loader is gone:
    // it must not touch the loader's owner. It, and what it captured, is destroyed with the
    // worker, on whichever thread lets go last.
    using Transform = std::function<std::shared_ptr<void>(const Preview&)>;
    // Runs on the worker under the loader's lock: it hands the result over (posts it to the
    // owner's thread) and must not call back into the loader. Destroyed by ~PreviewLoader.
    using Deliver = std::function<void(PreviewResult)>;

    PreviewLoader(std::unique_ptr<stein::fs::Reader> reader, PreviewLimits limits, Transform transform, Deliver deliver);
    // Never waits for a read or a transform: the worker is told to stop and left to finish the
    // call it is in; it keeps the reader alive until then. Nothing is delivered once this returned.
    ~PreviewLoader();
    PreviewLoader(const PreviewLoader&) = delete;
    PreviewLoader& operator=(const PreviewLoader&) = delete;

    // Supersedes every earlier request: one in progress is cancelled at its next chunk, one
    // waiting is dropped, and neither is delivered. Returns the request's ticket: increasing, never 0.
    std::uint64_t request(PreviewTarget target);
    // Supersedes every earlier request without making a new one.
    void cancel();
    PreviewActivity activity() const;

    // Workers of every loader that have not returned yet, those whose loader is gone included.
    static std::size_t workersAlive();
    // Waits until none is left, at most `patience`; false when some still run. For a process
    // about to run its static destructors: a transform may be inside a library they take down.
    static bool waitForWorkers(std::chrono::milliseconds patience);

private:
    struct State;
    std::shared_ptr<State> m_state;
};

} // namespace drstein::core
