#pragma once

#include <mutex>
#include <shared_mutex>

namespace cc
{
    using std::lock_guard;
    using std::mutex;
    using std::cv_status;

    using std::shared_timed_mutex;

    // unique_lock<shared_timed_mutex> scopeLock(my_shared_timed_mutex);
    using std::unique_lock;

    // shared_lock<shared_timed_mutex> scopeLock(my_shared_timed_mutex);
    using std::shared_lock;
} // namespace shared
