#include "ground/wave/BoundedLruCache.h"

#include <QString>

#include <cstdlib>
#include <iostream>

namespace
{
void require(bool condition, const char *message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}
}

int main()
{
    BoundedLruCache<QString, int> cache(3);
    cache.insert(QStringLiteral("a"), 1);
    cache.insert(QStringLiteral("b"), 2);
    cache.insert(QStringLiteral("c"), 3);

    int value = 0;
    require(cache.find(QStringLiteral("a"), &value) && value == 1,
            "cache hit returns the stored value");
    cache.insert(QStringLiteral("d"), 4);

    require(cache.size() == 3, "cache never exceeds its configured capacity");
    require(!cache.contains(QStringLiteral("b")), "least recently used entry is evicted");
    require(cache.contains(QStringLiteral("a")), "a cache hit refreshes recency");
    require(cache.contains(QStringLiteral("c")) && cache.contains(QStringLiteral("d")),
            "newer entries remain cached");

    std::cout << "bounded_lru_cache_test passed\n";
    return 0;
}
