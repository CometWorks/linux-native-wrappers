// Opt-in call counters for the Havok wrapper, used to measure which exports
// and callbacks a game session actually reaches. Built only with
// -DHAVOK_CALL_COUNTS=ON; otherwise HAVOK_COUNT_CALL expands to nothing.
//
// At runtime the counts go to the file named by LNW_HAVOK_CALL_COUNTS, which
// is rewritten every 5 seconds and at exit. Each line is
// `name<TAB>calls<TAB>calls from other threads`, where "other threads" means
// any thread but the one that made the first counted call.
#pragma once

#ifndef HAVOK_CALL_COUNTS

#define HAVOK_COUNT_CALL(name)

#else

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <pthread.h>
#include <string>
#include <thread>

struct havok_call_counter {
    const char *name;
    std::atomic<uint64_t> calls{0};
    std::atomic<uint64_t> other_thread_calls{0};
    havok_call_counter *next = nullptr;

    explicit havok_call_counter(const char *counter_name);
    void hit();
};

static std::atomic<havok_call_counter *> g_havok_counters{nullptr};
static std::atomic<pthread_t> g_havok_first_thread{0};

static void havok_write_call_counts()
{
    const char *path = std::getenv("LNW_HAVOK_CALL_COUNTS");
    if (!path || !*path) {
        return;
    }
    std::string tmp = std::string(path) + ".tmp";
    FILE *f = std::fopen(tmp.c_str(), "w");
    if (!f) {
        return;
    }
    for (auto *c = g_havok_counters.load(std::memory_order_acquire); c; c = c->next) {
        std::fprintf(f, "%s\t%llu\t%llu\n", c->name,
                     (unsigned long long) c->calls.load(std::memory_order_relaxed),
                     (unsigned long long) c->other_thread_calls.load(std::memory_order_relaxed));
    }
    std::fclose(f);
    std::rename(tmp.c_str(), path);
}

static void havok_start_count_writer()
{
    static std::atomic<bool> started{false};
    if (started.exchange(true)) {
        return;
    }
    std::atexit(havok_write_call_counts);
    std::thread([] {
        for (;;) {
            std::this_thread::sleep_for(std::chrono::seconds(5));
            havok_write_call_counts();
        }
    }).detach();
}

inline havok_call_counter::havok_call_counter(const char *counter_name) : name(counter_name)
{
    pthread_t none = 0;
    g_havok_first_thread.compare_exchange_strong(none, pthread_self());
    next = g_havok_counters.load(std::memory_order_relaxed);
    while (!g_havok_counters.compare_exchange_weak(next, this, std::memory_order_release,
                                                   std::memory_order_relaxed)) {
    }
    havok_start_count_writer();
}

inline void havok_call_counter::hit()
{
    calls.fetch_add(1, std::memory_order_relaxed);
    if (!pthread_equal(pthread_self(), g_havok_first_thread.load(std::memory_order_relaxed))) {
        other_thread_calls.fetch_add(1, std::memory_order_relaxed);
    }
}

#define HAVOK_COUNT_CALL(name) { static havok_call_counter havok_counter_(name); havok_counter_.hit(); }

#endif
