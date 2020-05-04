/*
 * Copyright (C) 2020 Codership Oy <info@codership.com>
 */

/**
 * This is to benchmark some iteration/erase/insert operations of gu::DecMap
 * and comparing those to std::map
 */

#define NDEBUG 1

#include <map>
#include "../src/gu_deqmap.hpp"
#include "../src/gu_limits.h"    // GU_PAGE_SIZE

#include <sys/time.h>
#include <iostream>
#include <sstream>
#include <fstream>
#include <vector>
#include <cstdlib>
#include <stdint.h>

static double time_diff(const struct timeval& l,
                        const struct timeval& r)
{
    double const left(double(l.tv_usec)*1.0e-06 + l.tv_sec);
    double const right(double(r.tv_usec)*1.0e-06 + r.tv_sec);
    return left - right;
}

typedef int64_t Key;
typedef int64_t Val;

typedef std::map  <Key, Val> StdMap;
typedef gu::DeqMap<Key, Val> DeqMap;

#if 0 // unused ATM
static inline std::ostream&
operator<<(std::ostream& os, const StdMap& m)
{
    os << "std::map(size: " << m.size();
    os << ", begin: ";
    m.size() ? os << m.begin()->first : os << "n/a";
    os << ", end: ";
    m.size() ? os << m.rbegin()->first : os << "n/a";
    os << ", front: ";
    m.size() ? os << m.begin()->second : os << "n/a";
    os << ", back: ";
    m.size() ? os << m.rbegin()->second : os << "n/a";
    os << ')';

    return os;
}
#endif

static inline Val&
iterator2ref(DeqMap::iterator it) { return *it; }
static inline Val&
iterator2ref(DeqMap::reverse_iterator it) { return *it; }

static inline Val&
iterator2ref(StdMap::iterator it) { return it->second; }
static inline Val&
iterator2ref(StdMap::reverse_iterator it) { return it->second; }

template <typename Map> struct FillByInsert;

template <>
struct FillByInsert<StdMap>
{
    FillByInsert(StdMap& map, Key const size)
    {
        StdMap::iterator hint(map.begin());
        for (Key i(0); i < size; ++i)
        {
            hint = map.insert(hint, std::pair<Key, Val>(i, i));
        }
    }
};

template <>
struct FillByInsert<DeqMap>
{
    FillByInsert(DeqMap& map, Key const size)
    {
        for (Key i(0); i < size; ++i) { map.insert(map.end(), i); }
    }
};

template <typename Map> struct FillByIndex;

template <>
struct FillByIndex<DeqMap>
{
    FillByIndex(DeqMap& map, Key const size)
    {
        for (Key i(0); i < size; ++i) { map.insert(i, i); }
    }
};

template <typename Map>
struct FillByPushFront
{
    FillByPushFront(Map& map, Key const size)
    {
        map.clear(size); // initialize begin_
        for (Key i(size - 1); i >= 0; --i) { map.push_front(i); }
    }
};

template <typename Map>
struct FillByPushBack
{
    FillByPushBack(Map& map, Key const size)
    {
        for (Key i(0); i < size; ++i) { map.push_back(i); }
    }
};

template <typename Map>
struct AccessByDirectIterator
{
    AccessByDirectIterator(Map& map, Key)
    {
        Key val(reinterpret_cast<intptr_t>(&map));
        for (typename Map::iterator it(map.begin()); it != map.end();
             ++it, ++val)
        {
            iterator2ref(it) = val;
        }
    }
};

template <typename Map>
struct AccessByReverseIterator
{
    AccessByReverseIterator(Map& map, Key)
    {
        Key val(reinterpret_cast<intptr_t>(&map));
        for (typename Map::reverse_iterator it(map.rbegin()); it != map.rend();
             ++it, ++val)
        {
            iterator2ref(it) = val;
        }
    }
};

template <typename Map> struct AccessByRandom; // operator[]

template <>
struct AccessByRandom<StdMap>
{
    AccessByRandom(StdMap& map, Key)
    {
        Key val(reinterpret_cast<intptr_t>(&map));
        for (Key i(map.begin()->first); i <= map.rbegin()->first; ++i, ++val)
        {
            map[i] = val;
        }
    }
};

template <>
struct AccessByRandom<DeqMap>
{
    AccessByRandom(DeqMap& map, Key)
    {
        Key val(reinterpret_cast<intptr_t>(&map));
        for (Key i(map.index_begin()); i < map.index_end(); ++i, ++val)
        {
            map.insert(i, val);
        }
    }
};

template <typename Map> struct RotateByEraseInsert;

template <>
struct RotateByEraseInsert<StdMap>
{
    RotateByEraseInsert(StdMap& map, Key)
    {
        Key const first(map.rbegin()->first + 1);
        Key const last (first + map.size());
        StdMap::iterator hint(--map.end());

        for (Key next(first); next < last; ++next)
        {
            map.erase(map.begin());
            hint = map.insert(hint, std::pair<Key, Val>(next, next));
        }
    }
};

template <>
struct RotateByEraseInsert<DeqMap>
{
    RotateByEraseInsert(DeqMap& map, Key)
    {
        Key const first(map.index_end());
        Key const last (first + map.size());

        for (Key next(first); next < last; ++next)
        {
            map.erase(map.begin());
            map.insert(map.end(), next);
        }
    }
};

template <typename Map> struct RotateByPopPush;

template <>
struct RotateByPopPush<DeqMap>
{
    RotateByPopPush(DeqMap& map, Key)
    {
        Key const first(map.index_end());
        Key const last (first + map.size());

        for (Key next(first); next < last; ++next)
        {
            map.pop_front();
            map.push_back(next);
        }
    }
};

template <typename Map> struct ClearByClear;

template <>
struct ClearByClear<StdMap>
{
    ClearByClear(StdMap& map, Key) { map.clear(); }
};

template <>
struct ClearByClear<DeqMap>
{
    ClearByClear(DeqMap& map, Key) { map.clear(0); }
};

template <typename Map>
struct ClearByErase
{
    ClearByErase(Map& map, Key) { map.erase(map.begin(), map.end()); }
};

template <typename Map>
struct ClearByDirectIterator
{
    ClearByDirectIterator(Map& map, Key)
    {
        while(!map.empty()) { map.erase(map.begin()); }
    }
};

template <typename Map>
struct ClearByReverseIterator
{
    ClearByReverseIterator(Map& map, Key)
    {
        while(!map.empty()) { map.erase(--map.end()); }
    }
};

template <typename Map>
struct ClearByPopFront
{
    ClearByPopFront(Map& map, Key) { while(!map.empty()) { map.pop_front(); } }
};

template <typename Map>
struct ClearByPopBack
{
    ClearByPopBack(Map& map, Key) { while(!map.empty()) { map.pop_back(); } }
};

template <typename Map, template <typename> class Operation>
double
timing(Map& map, Key size)
{
    struct timeval tv_begin, tv_end;

    gettimeofday(&tv_begin, NULL);

    Operation<Map>(map, size);

    gettimeofday(&tv_end, NULL);

    return time_diff(tv_end, tv_begin);
}

static void
mem_stats_bytes(double& VmSize, double& VmRSS, double& VmData)
{
    static size_t const page_size(GU_PAGE_SIZE);

    int size, rss, shared, text, unused, data;
    std::ifstream statm("/proc/self/statm");
    statm >> size >> rss >> shared >> text >> unused >> data;
    statm.close();

    VmSize = size * page_size;
    VmRSS  = rss  * page_size;
    VmData = data * page_size;
}

struct Metric
{
    double      val;
    int         count;

    void record(double t) { val += t; ++count; }
};

// associative array that maps metric string id to a vector of metric records
// for different container sizes
typedef std::map<const char*, std::vector<Metric> > Metrics;

static void
record(Metrics& m, const char* const id, size_t const power, double const val)
{
    std::vector<Metric>& v(m[id]);

    // this check is done every time here in order to be able to add records
    // ad hoc, without the need to know overall testing plan
    if (power >= v.size()) v.resize(power + 1);

    v[power].record(val);
}

static void
record_mem_stats(Metrics& m, size_t const power)
{
    double VmSize, VmRSS, VmData;
    mem_stats_bytes(VmSize, VmRSS, VmData);

#define RECORD(STAT) record(m, #STAT"/byte", power, STAT/sizeof(Val));
    RECORD(VmSize);
    RECORD(VmRSS);
    RECORD(VmData);
#undef RECORD
}

#define ASSERT_SIZE(S)                                                  \
    if (map.size() != size_t(S)) {                                      \
        std::cout << "ASSERT_SIZE failed: expected: " << S << ", found: " \
                  << map.size() << " at line " << __LINE__ << std::endl; \
        abort();                                                        \
    }

// benchmarking loop for std::map container
struct StdLoop
{
    static void run(Metrics& m, Key const size, int const power)
    {
#define MEASURE(OP) record(m, #OP, power, timing<StdMap, OP>(map, size));
        {
            StdMap map;
            MEASURE(FillByInsert           );
            ASSERT_SIZE(size);
            MEASURE(AccessByDirectIterator );
            MEASURE(AccessByReverseIterator);
            record_mem_stats(m, power);
            MEASURE(AccessByRandom         );
            MEASURE(ClearByClear           );
            ASSERT_SIZE(0);
        }
        {
            StdMap map;
            MEASURE(FillByInsert           );
            MEASURE(RotateByEraseInsert    );
            record_mem_stats(m, power);
            ASSERT_SIZE(size);
            MEASURE(ClearByErase           );
            ASSERT_SIZE(0);
        }
        {
            StdMap map;
            MEASURE(FillByInsert           );
            MEASURE(RotateByEraseInsert    );
            record_mem_stats(m, power);
            ASSERT_SIZE(size);
            MEASURE(ClearByDirectIterator  );
            ASSERT_SIZE(0);
        }
        {
            StdMap map;
            MEASURE(FillByInsert           );
            MEASURE(RotateByEraseInsert    );
            record_mem_stats(m, power);
            ASSERT_SIZE(size);
            MEASURE(ClearByReverseIterator );
            ASSERT_SIZE(0);
        }
#undef MEASURE
    }
}; // struct StdLoop

// benchmarking loop for gu::DeqMap container
struct DeqLoop
{
    static void run(Metrics& m, Key const size, int const power)
    {
#define MEASURE(OP) record(m, #OP, power, timing<DeqMap, OP>(map, size));
        {
            DeqMap map(0);
            MEASURE(FillByInsert           );
            ASSERT_SIZE(size);
            MEASURE(AccessByDirectIterator );
            MEASURE(AccessByReverseIterator);
            record_mem_stats(m, power);
            MEASURE(AccessByRandom         );
            MEASURE(RotateByEraseInsert    );
            ASSERT_SIZE(size);
            MEASURE(ClearByClear           );
            ASSERT_SIZE(0);
        }
        {
            DeqMap map(0);
            MEASURE(FillByPushFront        );
            MEASURE(RotateByEraseInsert    );
            record_mem_stats(m, power);
            ASSERT_SIZE(size);
            MEASURE(ClearByPopFront        );
            ASSERT_SIZE(0);
        }
        {
            DeqMap map(0);
            MEASURE(FillByPushBack         );
            MEASURE(RotateByEraseInsert    );
            record_mem_stats(m, power);
            ASSERT_SIZE(size);
            MEASURE(ClearByPopBack         );
            ASSERT_SIZE(0);
        }
        {
            DeqMap map(0);
            MEASURE(FillByIndex            );
            MEASURE(RotateByPopPush        );
            record_mem_stats(m, power);
            ASSERT_SIZE(size);
            MEASURE(ClearByErase           );
            ASSERT_SIZE(0);
        }
        {
            DeqMap map(0);
            MEASURE(FillByInsert           );
            MEASURE(RotateByPopPush        );
            record_mem_stats(m, power);
            ASSERT_SIZE(size);
            MEASURE(ClearByDirectIterator  );
            ASSERT_SIZE(0);
        }
        {
            DeqMap map(0);
            MEASURE(FillByIndex            );
            MEASURE(RotateByPopPush        );
            record_mem_stats(m, power);
            ASSERT_SIZE(size);
            MEASURE(ClearByReverseIterator );
            ASSERT_SIZE(0);
        }
        {
            DeqMap map(0);
            MEASURE(FillByPushBack         );
        }
        {
            DeqMap map(0);
            MEASURE(FillByPushFront        );
        }
#undef MEASURE
    }
};

#undef ASSERT_SIZE

static Key
power_size(Key base_size, int power) { return base_size << power; }

static void
print_metrics(Metrics& metrics, Key const base_size, const char* const title)
{
    std::cout << "================" << std::endl;
    std::cout << title << std::endl;

    int const columns(metrics.begin()->second.size());
    std::cout << "Size(M):";
    for (int c(0); c < columns; ++c) { std::cout << '\t' << (1 << c); }
    std::cout << std::endl;

    for (Metrics::iterator m(metrics.begin()); m != metrics.end(); ++m)
    {
        std::cout << m->first;
        std::vector<Metric>& v(m->second);

        for (size_t p(0); p < v.size(); ++p)
        {
            std::cout << '\t' << v[p].val/v[p].count/power_size(base_size, p);
        }

        std::cout << std::endl;
    }
    std::cout << "----------------" << std::endl;
}

template <typename Loop>
void loops(Key const base_size, int const base_loops, int const max_power,
           const char* const title)
{
    Metrics m;
    Key const max_size(power_size(base_size, max_power));

    struct timeval tv_begin, tv_end;
    gettimeofday(&tv_begin, NULL);

    for (int power(0); power <= max_power; ++power)
    {
        std::cout << "Power: " << power << std::endl;

        Key const size(power_size(base_size, power));
        // compensate shorter sizes with more loops
        int const loops(base_loops * (max_size/size));
        std::cout << "Loops(" << loops << "):" << std::flush;
        for (int l(1); l <= loops; ++l)
        {
            Loop::run(m, size, power);
            std::cout << ' ' << l << std::flush;
        }
        std::cout << std::endl;
    }

    gettimeofday(&tv_end, NULL);
    std::cout << "Total time spent: " << time_diff(tv_end, tv_begin)
              << std::endl;

    print_metrics(m, base_size, title);
}

template <typename T> void
read_arg(char* argv[], int position, T& var)
{
    std::string arg(argv[position]);
    std::istringstream is(arg);
    is >> var;
}

int main(int argc, char* argv[])
{
    static const char* const DEQ = "deq";
    static const char* const MAP = "map";

    Key const base_size(1 << 20); // 1M
    std::string container(DEQ);
    int max_power(0);
    int base_loops(1);

    if (argc >= 2) read_arg(argv, 1, container);
    if (argc >= 3) read_arg(argv, 2, max_power);
    if (argc >= 4) read_arg(argv, 3, base_loops);

    std::cout << "Running with parameters: container type = " << container
              << ", max power = " << max_power
              << ", base loops = " << base_loops << '\n';

//    struct timeval tv_begin, tv_end;
//    gettimeofday(&tv_begin, NULL);

    if (container == DEQ)
        loops<DeqLoop>(base_size, base_loops, max_power, "gu::DeqMap");
    else if (container == MAP)
        loops<StdLoop>(base_size, base_loops, max_power, "std::map");
    else
    {
        std::cerr << "First option should be either '" << DEQ << "' or '"
                  << MAP << "'" << std::endl;
        return 1;
    }

//    gettimeofday(&tv_end, NULL);
//    std::cout << "Total time spent: " << time_diff(tv_end, tv_begin)
//              << std::endl;

    return 0;
}
