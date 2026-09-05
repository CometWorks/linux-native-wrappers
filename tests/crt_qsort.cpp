#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <thread>

#include "win_types.h"

using Compare = int (WINAPI *)(const void*, const void*);
extern void WINAPI crt_qsort(void*, size_t, size_t, Compare);

struct Entry { int key; int padding[3]; };

static int WINAPI ascending(const void* lhs, const void* rhs)
{
    int a = static_cast<const Entry*>(lhs)->key;
    int b = static_cast<const Entry*>(rhs)->key;
    return (a > b) - (a < b);
}

static int WINAPI descending(const void* lhs, const void* rhs)
{
    return -ascending(lhs, rhs);
}

static void check(bool condition)
{
    if (!condition) { std::fputs("CRT qsort check failed\n", stderr); std::abort(); }
}

static int WINAPI nested(const void* lhs, const void* rhs)
{
    Entry inner[] = {{2, {}}, {1, {}}, {3, {}}};
    crt_qsort(inner, 3, sizeof(Entry), descending);
    check(inner[0].key == 3 && inner[1].key == 2 && inner[2].key == 1);
    return ascending(lhs, rhs);
}

static void exercise()
{
    for (int pass = 0; pass < 100; ++pass) {
        Entry entries[] = {{4, {}}, {1, {}}, {3, {}}, {2, {}}, {2, {}}};
        crt_qsort(entries, 0, sizeof(Entry), ascending);
        crt_qsort(entries, 1, sizeof(Entry), ascending);
        check(entries[0].key == 4);
        crt_qsort(entries, 5, sizeof(Entry), pass % 2 ? nested : ascending);
        const int expected[] = {1, 2, 2, 3, 4};
        for (size_t i = 0; i < 5; ++i) check(entries[i].key == expected[i]);
    }
}

int main()
{
    exercise();
    std::array<std::thread, 4> workers;
    for (auto& worker : workers) worker = std::thread(exercise);
    for (auto& worker : workers) worker.join();
    std::puts("CRT qsort repeated, nested and concurrent checks passed");
}
