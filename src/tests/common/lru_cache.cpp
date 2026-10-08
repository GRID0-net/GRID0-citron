// SPDX-License-Identifier: GPL-2.0-or-later

#include <catch2/catch_test_macros.hpp>
#include "common/lru_cache.h"

namespace {
struct CacheTraits {
    using ObjectType = int;
    using TickType = u64;
};
}

TEST_CASE("LRU callback can stop eviction iteration", "[common][lru]") {
    Common::LeastRecentlyUsedCache<CacheTraits> cache;
    cache.Insert(1, 1);
    cache.Insert(2, 2);
    cache.Insert(3, 3);
    int visits = 0;
    cache.ForEachItemBelow(3, [&](int& object) {
        ++visits;
        object = 10;
        return true;
    });
    REQUIRE(visits == 1);
    visits = 0;
    cache.ForEachItemBelow(2, [&](int) { ++visits; });
    REQUIRE(visits == 2);
}
