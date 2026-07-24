// Copyright 2020 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>
#include <deque>

#include "common/horizon_thread.h"
#include "common/polyfill_thread.h"
#include "common/thread.h"
#include "common/unique_function.h"

namespace Common {

// Enforces a minimum gap between completions across every ThreadWorker that shares the same
// instance, not just within one pool -- two different pools each finishing a job within their
// own per-pool pace window can still land close together in wall-clock time otherwise.
class PaceLimiter {
public:
    explicit PaceLimiter(std::chrono::milliseconds min_gap_) : base_gap{min_gap_} {}

    // Lets a caller that knows nothing else is competing for present/frame delivery right now
    // (e.g. a boot loading screen) skip the wait entirely without tearing down the limiter.
    void SetBypassed(bool bypassed_) {
        bypassed.store(bypassed_, std::memory_order_relaxed);
    }

    void ConfigureBacklogScaling(std::function<std::size_t()> backlog_provider_,
                                 std::size_t low_backlog, std::size_t high_backlog,
                                 std::chrono::milliseconds floor_gap_) {
        backlog_provider = std::move(backlog_provider_);
        low = low_backlog;
        high = high_backlog;
        floor_gap = floor_gap_;
    }

    void Pace() {
        if (bypassed.load(std::memory_order_relaxed)) {
            return;
        }
        const auto gap = CurrentGap();
        if (gap.count() <= 0) {
            return;
        }
        std::unique_lock lock{mutex};
        const auto now = std::chrono::steady_clock::now();
        const auto since_last = now - last_completion;
        if (since_last < gap) {
            const auto remaining = gap - since_last;
            lock.unlock();
            std::this_thread::sleep_for(remaining);
            lock.lock();
        }
        last_completion = std::chrono::steady_clock::now();
    }

private:
    std::chrono::milliseconds CurrentGap() const {
        if (!backlog_provider || high <= low) {
            return base_gap;
        }
        const std::size_t pending = backlog_provider();
        if (pending <= low) {
            return base_gap;
        }
        if (pending >= high) {
            return floor_gap;
        }
        const double t = static_cast<double>(pending - low) / static_cast<double>(high - low);
        const auto range = base_gap - floor_gap;
        return base_gap - std::chrono::milliseconds{static_cast<long long>(t * range.count())};
    }

    std::mutex mutex;
    std::chrono::milliseconds base_gap;
    std::chrono::milliseconds floor_gap{base_gap};
    std::size_t low{std::numeric_limits<std::size_t>::max()};
    std::size_t high{std::numeric_limits<std::size_t>::max()};
    std::function<std::size_t()> backlog_provider;
    std::chrono::steady_clock::time_point last_completion{};
    std::atomic<bool> bypassed{false};
};

template <class StateType = void>
class StatefulThreadWorker {
    static constexpr bool with_state = !std::is_same_v<StateType, void>;

    struct DummyCallable {
        int operator()(std::size_t) const noexcept {
            return 0;
        }
    };

    using Task =
        std::conditional_t<with_state, UniqueFunction<void, StateType*>, UniqueFunction<void>>;
    using StateMaker =
        std::conditional_t<with_state, std::function<StateType(std::size_t)>, DummyCallable>;

public:
    // `preferred_cores` optionally pins workers to cores from this list, round-robining each
    // worker's index across it so a multi-worker pool spreads out instead of every worker
    // funneling onto the list's first available core.
    explicit StatefulThreadWorker(std::size_t num_workers, std::string_view name,
                                  StateMaker func = {},
                                  std::vector<std::uint32_t> preferred_cores = {},
                                  Common::ThreadPriority priority = Common::ThreadPriority::Normal,
                                  std::shared_ptr<PaceLimiter> pacer = {},
                                  std::size_t lifo_threshold = 0,
                                  std::size_t starvation_guard_interval = 5)
        : workers_queued{num_workers}, thread_name{name} {
        const auto lambda = [this, func, cores = std::move(preferred_cores), priority, pacer,
                             lifo_threshold,
                             starvation_guard_interval](std::stop_token stop_token,
                                                        std::size_t index) {
            Common::SetCurrentThreadName(thread_name.data());
            Common::SetCurrentThreadPriority(priority);
            if (!cores.empty()) {
                const std::uint32_t assigned = cores[index % cores.size()];
                if (!Common::Horizon::PinCurrentThread(assigned)) {
                    for (const std::uint32_t core_id : cores) {
                        if (Common::Horizon::PinCurrentThread(core_id)) {
                            break;
                        }
                    }
                }
            }
            {
                [[maybe_unused]] std::conditional_t<with_state, StateType, int> state{func(index)};
                while (!stop_token.stop_requested()) {
                    Task task;
                    {
                        std::unique_lock lock{queue_mutex};
                        if (requests.empty()) {
                            wait_condition.notify_all();
                        }
                        Common::CondvarWait(condition, lock, stop_token,
                                            [this] { return !requests.empty(); });
                        if (stop_token.stop_requested()) {
                            break;
                        }
                        bool take_oldest = true;
                        if (lifo_threshold > 0 && requests.size() > lifo_threshold) {
                            ++lifo_pulls_since_oldest;
                            take_oldest = starvation_guard_interval > 0 &&
                                         lifo_pulls_since_oldest >= starvation_guard_interval;
                        }
                        if (take_oldest) {
                            lifo_pulls_since_oldest = 0;
                            task = std::move(requests.front());
                            requests.pop_front();
                        } else {
                            task = std::move(requests.back());
                            requests.pop_back();
                        }
                    }
                    if constexpr (with_state) {
                        task(&state);
                    } else {
                        task();
                    }
                    ++work_done;
                    if (pacer) {
                        pacer->Pace();
                    }
                }
            }
            ++workers_stopped;
            wait_condition.notify_all();
        };
        threads.reserve(num_workers);
        for (std::size_t i = 0; i < num_workers; ++i) {
            threads.emplace_back(lambda, i);
        }
    }

    StatefulThreadWorker& operator=(const StatefulThreadWorker&) = delete;
    StatefulThreadWorker(const StatefulThreadWorker&) = delete;

    StatefulThreadWorker& operator=(StatefulThreadWorker&&) = delete;
    StatefulThreadWorker(StatefulThreadWorker&&) = delete;

    void QueueWork(Task work) {
        {
            std::unique_lock lock{queue_mutex};
            requests.emplace_back(std::move(work));
            ++work_scheduled;
        }
        condition.notify_one();
    }

    void WaitForRequests(std::stop_token stop_token = {}) {
        std::stop_callback callback(stop_token, [this] {
            for (auto& thread : threads) {
                thread.request_stop();
            }
        });
        std::unique_lock lock{queue_mutex};
        wait_condition.wait(lock, [this] {
            return workers_stopped >= workers_queued || work_done >= work_scheduled;
        });
    }

    const std::size_t NumWorkers() const noexcept {
        return threads.size();
    }

private:
    std::deque<Task> requests;
    // Guarded by queue_mutex, same as requests -- counts consecutive LIFO pulls since the last
    // time the oldest item was serviced, so the starvation guard can trigger every Nth pull.
    std::size_t lifo_pulls_since_oldest{0};
    std::mutex queue_mutex;
    std::condition_variable_any condition;
    std::condition_variable wait_condition;
    std::atomic<std::size_t> work_scheduled{};
    std::atomic<std::size_t> work_done{};
    std::atomic<std::size_t> workers_stopped{};
    std::atomic<std::size_t> workers_queued{};
    std::string_view thread_name;
    std::vector<std::jthread> threads;
};

using ThreadWorker = StatefulThreadWorker<>;

} // namespace Common
