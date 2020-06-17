// Copyright (C) 2020 Codership Oy <info@codership.com>

#define GU_DEQMAP_CONSISTENCY_CHECKS 1

#include "../src/gu_deqmap.hpp"

#include "gu_deqmap_test.hpp"

#include <cstdlib> // rand()

class Test
{
public:
    Test() : val_(-1) {} // Null object

    explicit
    Test(int v) : val_(v) {}

    bool
    operator ==(const Test& other) const { return val_ == other.val_; }

    int
    operator +() const { return val_; }

private:
    int val_;
};

static std::ostream&
operator <<(std::ostream& os, const Test& t) { os << +t; return os; }

START_TEST(ctor_clear)
{
    typedef gu::DeqMap<char, Test> Map;
    Map m(-1);

    fail_if(m.size() > 0);
    fail_if(m.index_begin() != -1);
    fail_if(m.index_end() != -1);

    fail_if(m.upper_bound(0) != m.index_end());

    m.clear(5);

    fail_if(m.size() > 0);
    fail_if(m.index_begin() != 5);
    fail_if(m.index_end() != 5);

    fail_if(m.upper_bound(0) != m.index_begin());
}
END_TEST

START_TEST(push_pop)
{
    typedef gu::DeqMap<char, char> Map;
    Map m(-1);

    /* some push acton */
    m.push_back(1); // -1
    m.push_back(2); //  0
    m.push_back(3); //  1
    m.push_front(4);// -2
    /* Here we have: 4, 1, 2, 3 */

    fail_if(m.size()  != 4);
    fail_if(m.front() != 4);
    fail_if(m.back()  != 3);
    fail_if(m.index_begin() != -2);
    fail_if(m.index_end()   != 2);

    m.pop_front();
    /* Here we have: 1, 2, 3 */

    fail_if(m.size()  != 3);
    fail_if(m.front() != 1);
    fail_if(m.back()  != 3);
    fail_if(m.index_begin() != -1);
    fail_if(m.index_end()   != 2);

    m.pop_back();
    /* Here we have: 1, 2 */

    fail_if(m.size()  != 2);
    fail_if(m.front() != 1);
    fail_if(m.back()  != 2);
    fail_if(m.index_begin() != -1);
    fail_if(m.index_end()   != 1);

    m.pop_front();
    m.pop_front();
    /* Here we have: empty */

    fail_if(m.size()  != 0);
    fail_if(m.index_begin() != 1);
    fail_if(m.index_end()   != 1);

    m.push_front(7); // 0

    fail_if(m.size()  != 1);
    fail_if(m.front() != 7);
    fail_if(m.back()  != 7);
    fail_if(m.index_begin() != 0);
    fail_if(m.index_end()   != 1);

    m.pop_back();

    fail_if(m.size()  != 0);
    fail_if(m.index_begin() != 0);
    fail_if(m.index_end()   != 0);
}
END_TEST

START_TEST(pop_holes) /* autoshrinking when popping on container with holes */
{
    typedef gu::DeqMap<char, char> Map;
    Map m(-1);

    fail_if(m.size() != 0);
    fail_if(m.index_begin() != -1);
    fail_if(m.index_end()   != -1);
    fail_if(m.index_back()  != -2);

    m.insert(1, 1);
    m.insert(4, 4);

    fail_if(m.size() != 4, "expected 4 got %u", m.size());
    fail_if(m.index_begin() != 1);
    fail_if(m.index_end()   != 5);
    fail_if(m.index_back()  != 4);
    fail_if(!Map::not_set(m[2]));
    fail_if(!Map::not_set(m[3]));

    m.pop_front();

    fail_if(m.size() != 1);
    fail_if(m.index_begin() != 4);
    fail_if(m.index_end()   != 5);
    fail_if(m.index_back()  != 4);
    fail_if(*m.begin() != 4);

    m.insert(1, 1);

    fail_if(m.size() != 4);
    fail_if(m.index_begin() != 1);
    fail_if(m.index_end()   != 5);
    fail_if(m.index_back()  != 4);
    fail_if(!Map::not_set(m[2]));
    fail_if(!Map::not_set(m[3]));

    m.pop_back();

    fail_if(m.size() != 1);
    fail_if(m.index_begin() != 1);
    fail_if(m.index_end()   != 2);
    fail_if(m.index_back()  != 1);
    fail_if(*m.begin() != 1);
}
END_TEST

START_TEST(at)
{
    typedef gu::DeqMap<char, char> Map;
    Map m(-1);

    try
    {
        m.at(-1);
        fail("expected exception");
    }
    catch (gu::NotFound&) {}

    m.push_back(3);

    try
    {
        fail_if(3 != m.at(-1));
    }
    catch (...)
    {
        fail("unexpected exception");
    }

    try
    {
        m.at(-2);
        fail("expected exception");
    }
    catch (gu::NotFound&) {}

    try
    {
        m.at(0);
        fail("expected exception");
    }
    catch (gu::NotFound&) {}
}
END_TEST

START_TEST(iterators_insert)
{
    typedef gu::DeqMap<char, char> Map;
    Map m(-1);

    m.insert(m.begin(), 4, 4);
    /* here we have 4, 4, 4, 4 */

    fail_if(m.size() != 4);
    fail_if(*m.begin()  != 4);
    fail_if(*m.rbegin() != 4);
    fail_if(m.index_begin() != -1);
    fail_if(m.index_end()   != 3);

    m.insert(m.begin() + 1, 2, 2); // bulk insert (overwrite) in the middle
    /* here we have 4, 2, 2, 4 */

    fail_if(m.size() != 4);
    fail_if(*m.begin()  != 4);
    fail_if(*m.rbegin() != 4);
    fail_if(m.index_begin() != -1);
    fail_if(m.index_end()   != 3);

    m.insert(m.begin(), 2, 1); // bulk insert (overwrite) in the beginning
    /* here we have 1, 1, 2, 4 */

    fail_if(m.size() != 4);
    fail_if(*m.begin()  != 1);
    fail_if(*m.rbegin() != 4);
    fail_if(m.index_begin() != -1);
    fail_if(m.index_end()   != 3);

    m.insert(m.end(), 2, 5); // bulk insert in the end
    /* here we have 1, 1, 2, 4, 5, 5 */

    fail_if(m.size() != 6);
    fail_if(*m.begin()  != 1);
    fail_if(*m.rbegin() != 5);
    fail_if(m.index_begin() != -1);
    fail_if(m.index_end()   != 5);

    m.insert(m.begin(), -1); // single insert (overwrite) in the beginning
    /* here we have -1, 1, 2, 4, 5, 5 */

    fail_if(m.size() != 6);
    fail_if(*m.begin()  != -1);
    fail_if(*m.rbegin() != 5);
    fail_if(m.index_begin() != -1);
    fail_if(m.index_end()   != 5);

    Map::iterator b(m.begin());
    ++b;
    fail_if(Map::not_set(*b));
    fail_if(*b != 1);
    fail_if(m.index(b) != 0);
    ++b;
    fail_if(*b != 2);
    fail_if(m.index(b) != 1);

    m.insert(b, 1); // single insert (overwrite) in the middle
    /* here we have -1, 1, 1, 4, 5, 5 */

    fail_if(m.size() != 6);
    fail_if(*b != 1);

    m.insert(m.end(), 6); // single insert in the end
    /* here we have -1, 1, 1, 4, 5, 5, 6 */

    fail_if(m.size() != 7);
    fail_if(*m.begin()  != -1);
    fail_if(*m.rbegin() != 6);
    fail_if(m.index_begin() != -1);
    fail_if(m.index_end()   != 6);
    fail_if(m.index_back()  != 5);
    fail_if(m[m.index_back()] != 6);

    ++b;
    fail_if(*b != 4);
    fail_if(m.index(b) != 2);
    *b = 2; // assignment via iterator
    /* here we have -1, 1, 1, 2, 5, 5, 6 */

    fail_if(m.size() != 7);
    fail_if(*b != 2);

    Map::reverse_iterator rb(m.rbegin());
    fail_if(*rb != 6);
    fail_if(m.index(rb) != 5);

    *rb = 5;
    /* here we have -1, 1, 1, 2, 5, 5, 5 */
    fail_if(*rb != 5);

    ++rb;
    fail_if(*rb != 5);
    fail_if(m.index(rb) != 4);

    *rb = 4;
    /* here we have -1, 1, 1, 2, 5, 4, 5 */
    fail_if(*rb != 4);
}
END_TEST

START_TEST(iterators_erase)
{
    typedef gu::DeqMap<char, char> Map;
    Map m(-1);

    Map::size_type init_size(12);
    m.insert(m.begin(), init_size, 1);
    /* here we have 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 */

    fail_if(m.size() != init_size);
    fail_if(m.index_begin() != -1);
    fail_if(m.index_end() != m.index_begin() + int(m.size()));
    fail_if(m.front() != 1);
    fail_if(m.back()  != 1);

    Map::iterator b(m.begin() + 2);
    m.erase(b); // single erase in the middle
    /* here we have 1, 1, N, 1, 1, 1, 1, 1, 1, 1, 1, 1 */
    /*                    b                            */
    fail_unless(Map::not_set(*b));

    Map::iterator e(m.end() - 4);
    m.erase(e, e + 2); // bulk erase in the middle
    /* here we have 1, 1, N, 1, 1, 1, 1, 1, N, N, 1, 1 */
    /*                    b                 e          */
    fail_unless(Map::not_set(*e));

    fail_if(m.size() != init_size);
    fail_if(m.index_begin() != -1);
    fail_if(m.index_end() != m.index_begin() + int(m.size()));
    fail_if(m.front() != 1);
    fail_if(m.back()  != 1);

    m.erase(m.begin()); // single erase at the front
    /* here we have    1, N, 1, 1, 1, 1, 1, N, N, 1, 1 */
    /*                    b                 e          */

    fail_if(m.size() != init_size - 1);
    fail_if(m.index_begin() != 0);
    fail_if(m.index_end() != m.index_begin() + int(m.size()));
    fail_if(m.front() != 1);
    fail_if(m.back()  != 1);

    m.erase(m.begin()); // single erase at the front before hole
    /* here we have          1, 1, 1, 1, 1, N, N, 1, 1 */
    /*                    b                 e          */

    fail_if(m.size() != init_size - 3);
    fail_if(m.index_begin() != 2);
    fail_if(m.index_end() != m.index_begin() + int(m.size()));
    fail_if(m.front() != 1);
    fail_if(m.back()  != 1);

    m.erase(m.end() - 1); // single erase at the back
    /* here we have          1, 1, 1, 1, 1, N, N, 1    */
    /*                    b                 e          */

    fail_if(m.size() != init_size - 4);
    fail_if(m.index_begin() != 2);
    fail_if(m.index_end() != m.index_begin() + int(m.size()));
    fail_if(m.front() != 1);
    fail_if(m.back()  != 1);

    m.erase(m.end() - 1); // single erase at the back before hole
    /* here we have          1, 1, 1, 1, 1             */
    /*                    b                 e          */

    fail_if(m.size() != init_size - 7);
    fail_if(m.index_begin() != 2);
    fail_if(m.index_end() != m.index_begin() + int(m.size()));
    fail_if(m.front() != 1);
    fail_if(m.back()  != 1);

    m.erase(m.begin(), m.begin() + 2); // bulk erase at the front
    /* here we have                1, 1, 1             */
    /*                    b                 e          */

    fail_if(m.size() != init_size - 9);
    fail_if(m.index_begin() != 4);
    fail_if(m.index_end() != m.index_begin() + int(m.size()));
    fail_if(m.front() != 1);
    fail_if(m.back()  != 1);

    m.erase(m.end() - 2, m.end()); // bulk erase at the end
    /* here we have                1                   */
    /*                    b                 e          */

    fail_if(m.size() != init_size - 11);
    fail_if(m.index_begin() != 4);
    fail_if(m.index_end() != m.index_begin() + int(m.size()));
    fail_if(m.front() != 1);
    fail_if(m.back()  != 1);

    m.insert(m.end(), 16, 1);
    init_size = m.size();
    fail_if (init_size != 17);
    /* here we have 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 */

    m.erase(m.begin() + 1, m.begin() + 3);
    /* here we have 1, N, N, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 */
    fail_if(m.size() != init_size);

    m.erase(m.begin() + 2, m.begin() + 4);
    /* here we have 1, N, N, N, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 */
    fail_if(m.size() != init_size);

    m.erase(m.begin(), m.begin() + 2);
    /* here we have             1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 */
    fail_if(m.size() != init_size - 4);
    fail_if(m.index_begin() != 8);

    m.erase(m.end() - 3, m.end() - 1);
    /* here we have             1, 1, 1, 1, 1, 1, 1, 1, 1, 1, N, N, 1 */
    fail_if(m.size() != init_size - 4);

    m.erase(m.end() - 4, m.end() - 1);
    /* here we have             1, 1, 1, 1, 1, 1, 1, 1, 1, N, N, N, 1 */
    fail_if(m.size() != init_size - 4);

    m.erase(m.end() - 2, m.end());
    /* here we have             1, 1, 1, 1, 1, 1, 1, 1, 1             */
    fail_if(m.size() != init_size - 8);

    m.erase(m.begin() + 2, m.begin() + 4);
    /* here we have             1, 1, N, N, 1, 1, 1, 1, 1             */
    fail_if(m.size() != init_size - 8);

    m.erase(m.begin() + 5, m.begin() + 7);
    /* here we have             1, 1, N, N, 1, N, N, 1, 1             */
    fail_if(m.size() != init_size - 8);
    fail_if(m.index_begin() != 8);

    fail_unless(Map::not_set(m[m.index_begin() + 2]));
    fail_unless(Map::not_set(m[m.index_begin() + 3]));
    fail_if    (Map::not_set(m[m.index_begin() + 4]));
    fail_unless(Map::not_set(m[m.index_begin() + 5]));
    fail_unless(Map::not_set(m[m.index_begin() + 6]));

    m.erase(m.begin() + 1, m.begin() + 8);
    /* here we have             1, N, N, N, N, N, N, N, 1             */
    fail_if(m.size() != init_size - 8);

    m.erase(m.begin());
    /* here we have                                     1             */
    fail_if(m.size() != 1);
    fail_if(m.index_begin() != 16);
}
END_TEST

/* Tests attempts to insert Null values to container of size SIZE. Two cases
 * for every insert method: beginning (equivalent to middle) and end */
static void
null_insertions(size_t const SIZE)
{
    typedef gu::DeqMap<char, Test> Map;
    Map::value_type const Null(Map::null_value());
    fail_unless(Map::null_value() == Null);
    fail_unless(Map::not_set(Null));

    Map::value_type const Default = Map::value_type(0);
    fail_if(Null == Default);
    fail_if(Map::not_set(Default));

    Map::index_type const Begin(-1);
    Map m(Begin);

    m.insert(m.end(), SIZE, Default);

    fail_if(m.size() != SIZE);
    fail_if(m.index_begin() != Begin);
    fail_if(m.index_end()   != m.index_begin() + int(m.size()));
    fail_unless((m.size() == 0 || m.front() == Default));
    fail_unless((m.size() == 0 || m.back()  == Default));

    try
    {
        m.push_front(Null);
        fail("No exception in push_front()");
    }
    catch (std::invalid_argument& e)
    {
        try
        {
            m.push_back(Null);
            fail("No exception in push_back()");
        }
        catch (std::invalid_argument& e)
        {
            fail_if(m.size() != SIZE);
            fail_if(m.index_begin() != Begin);
            fail_if(m.index_end()   != m.index_begin() + int(m.size()));
            fail_unless(m.size() == 0 || m.front() == Default);
            fail_unless(m.size() == 0 || m.back()  == Default);
        }
        catch (...)
        {
            fail("Unexpected exception in push_back()");
        }
    }
    catch (...)
    {
        fail("Unexpected exception in push_front()");
    }

    try
    {
        m.insert(m.begin(), Null);
        fail("No exception in insert() at the begin()");
    }
    catch (std::invalid_argument& e)
    {
        try
        {
            m.insert(m.end(), Null);
            fail("No exception in insert() at the end()");
        }
        catch (std::invalid_argument& e)
        {
            fail_if(m.size() != SIZE);
            fail_if(m.index_begin() != Begin);
            fail_if(m.index_end()   != m.index_begin() + int(m.size()));
            fail_unless(m.size() == 0 || m.front() == Default);
            fail_unless(m.size() == 0 || m.back()  == Default);
        }
        catch (...)
        {
            fail("Unexpected exception in insert() at the end()");
        }
    }
    catch (...)
    {
        fail("Unexpected exception in insert() at the begin()");
    }

    try
    {
        m.insert(m.begin(), 3, Null);
        fail("No exception in insert() at the begin()");
    }
    catch (std::invalid_argument& e)
    {
        try
        {
            m.insert(m.end(), 3, Null);
            fail("No exception in insert() at the end()");
        }
        catch (std::invalid_argument& e)
        {
            fail_if(m.size() != SIZE);
            fail_if(m.index_begin() != Begin);
            fail_if(m.index_end()   != m.index_begin() + int(m.size()));
            fail_unless(m.size() == 0 || m.front() == Default);
            fail_unless(m.size() == 0 || m.back()  == Default);
        }
        catch (...)
        {
            fail("Unexpected exception in insert() at the end()");
        }
    }
    catch (...)
    {
        fail("Unexpected exception in insert() at the begin()");
    }


    try
    {
        m.insert(m.index_begin(), Null);
        fail("No exception in insert() at the begin()");
    }
    catch (std::invalid_argument& e)
    {
        try
        {
            m.insert(m.index_end(), Null);
            fail("No exception in insert() at the end()");
        }
        catch (std::invalid_argument& e)
        {
            fail_if(m.size() != SIZE);
            fail_if(m.index_begin() != Begin);
            fail_if(m.index_end()   != m.index_begin() + int(m.size()));
            fail_unless(m.size() == 0 || m.front() == Default);
            fail_unless(m.size() == 0 || m.back()  == Default);
        }
        catch (...)
        {
            fail("Unexpected exception in insert() at the end()");
        }
    }
    catch (...)
    {
        fail("Unexpected exception in insert() at the begin()");
    }
}

START_TEST(null_insertions_0) /* tests null insertions to empty container */
{
    null_insertions(0);
}
END_TEST

START_TEST(null_insertions_1) /* tests null insertions to non empty container */
{
    null_insertions(1);
}
END_TEST

START_TEST(random_access)
{
    typedef gu::DeqMap<char, Test> Map;
    Map::index_type const Min(-5);
    Map::index_type  const Max(5);

    Map m(100);
    m.insert(Min, Test(Min));
    m.insert(Max, Test(Max));

    fail_if(m.size() != (Max - Min + 1));
    fail_if(m.index_begin() != Min);
    fail_if(m.index_back()  != Max);
    fail_unless(m.front() == Test(Min));
    fail_unless(m.back()  == Test(Max));

    for (Map::index_type i(Min + 1); i < Max; ++i)
    {
        fail_unless(Map::not_set(m[i]));
    }

    for (Map::index_type i(Min + 1); i < Max; ++i)
    {
        Map::value_type const val((Test(i)));

        if (!Map::not_set(val)) m.insert(i, val);
    }

    for (Map::index_type i(Min); i <= Max; ++i)
    {
        Map::value_type const val((Test(i)));

        fail_unless(m[i] == val);
        if (!Map::not_set(val)) fail_unless(m.at(i) == val);
    }
}
END_TEST

START_TEST(find)
{
    typedef gu::DeqMap<int, int> Map;
    Map m(0);

    m.insert(1, 1);
    m.insert(2, 2);
    m.insert(3, 3);

    fail_if(m.size()  != 3);
    fail_if(m[1] != 1);
    fail_if(m[3] != 3);

    for(Map::index_type i(m.index_begin()); i < m.index_end(); ++i)
    {
        fail_if(*m.find(i) != i);
        *m.find(i) = i + 1;
        fail_if(*m.find(i) != i + 1);
    }
    const Map& mc(m); // test const overload

    for(Map::index_type i(mc.index_begin()); i < mc.index_end(); ++i)
        fail_if(*mc.find(i) != i + 1);
}
END_TEST

START_TEST(random_test)
{
    /* access methods */
    typedef enum
    {
        ITERATOR,
        INDEX,
        PUSHPOP
    } how_t;

    typedef gu::DeqMap<int, int> Map;

    class map_state
    {
        Map::size_type  const size_;
        Map::index_type const index_begin_;
        Map::index_type const index_end_;
        Map::const_iterator   const begin_;
        Map::const_iterator   const end_;

    public:
        map_state(const Map& m) :
            size_       (m.size()),
            index_begin_(m.index_begin()),
            index_end_  (m.index_end()),
            begin_      (m.begin()),
            end_        (m.end())
        {}

        bool operator==(const map_state& o) const
        {
            return size_ == o.size_ && index_begin_ == o.index_begin_ &&
                index_end_ == o.index_end_ && begin_ == o.begin_ &&
                end_ == o.end_;
        }
        bool operator!=(const map_state& o) const { return !operator==(o); }
    };

    static int const SIZE(1<<14); // 16K
    static int const SEED(1);

    Map map(0);
    srand(SEED);

    /* Insert size elements into the map */
    for (int i(0); i < SIZE; ++i)
    {
        int const val(rand());
        int const idx(val % SIZE);

        int const begin(map.index_begin());
        int const end(map.index_end());

        bool size_change;
        how_t how;
        if (idx == end)
        {
            /* elements at the end can be inserted anyhow */
            how = how_t(rand() % (PUSHPOP + 1));
            size_change = true;
        }
        else if (begin <= idx && idx < end)
        {
            /* elements within the range can be inserted either by ITERATOR
             * or INDEX */
            how = how_t(rand() % (INDEX + 1));
            size_change = false;
        }
        else if (idx == begin - 1)
        {
            /* elements right in front can be inserted either by INDEX
             * or PUSHPOP */
            how = how_t(rand() % (INDEX + 1) + 1);
            size_change = true;
        }
        else
        {
            /* elements that are way out can be inserted only by INDEX */
            how = INDEX;
            size_change = true;
        }

        map_state const init_state(map);

        switch(how)
        {
        case ITERATOR:
        {
            Map::iterator it(map.begin() + (idx - begin));
            it = map.insert(it, val);
            fail_if(it == map.end());
            fail_if(init_state != map_state(map));
        }
        break;
        case INDEX:
            map.insert(idx, val);
            if (size_change)
                fail_if(init_state == map_state(map));
            else
                fail_if(init_state != map_state(map));

            break;
        case PUSHPOP:
            if (idx + 1 == begin)
                map.push_front(val);
            else
            {
                fail_if(idx != end);
                map.push_back(val);
            }

            fail_if(init_state == map_state(map));
        }
    }

    fail_if(map.empty());

    /* now erase all elements */
    while (!map.empty())
    {
        int const begin(map.index_begin());
        int const end(map.index_end());
        fail_if(begin >= end);

        int const size(end - begin);
        int const idx((rand() % size) + begin);

        bool size_change;
        how_t how;

        if (begin < idx && idx < (end - 1))
        {
            /* from inside we can erase either by ITERATOR or INDEX */
            how = how_t(rand() % (INDEX + 1));
            size_change = false;
        }
        else
        {
            /* from the edges we can erase anyhow */
            how = how_t(rand() % (PUSHPOP + 1));
            size_change = true;
        }

        if (!Map::not_set(map[idx]))
        {
            fail_if((map[idx]%SIZE) != idx, /* see filling loop above */
                    "Expected %d, got %d %% %zu = %d",
                    idx, map[idx], SIZE, map[idx]%SIZE);
        }

        map_state const init_state(map);

        switch (how)
        {
        case ITERATOR:
            map.erase(map.begin() + idx - begin);
            if (size_change)
                fail_if(init_state == map_state(map));
            else
                fail_if(init_state != map_state(map));

            break;
        case INDEX:
            map.erase(idx);
            if (size_change)
                fail_if(init_state == map_state(map));
            else
                fail_if(init_state != map_state(map));

            break;
        case PUSHPOP:
            if (idx == begin)
                map.pop_front();
            else
                map.pop_back();

            fail_if(init_state == map_state(map));
        }
    }
}
END_TEST

Suite* gu_deqmap_suite ()
{
    Suite* const s(suite_create("gu::DeqMap"));

    TCase* t;
    t = tcase_create("ctor_clear");
    tcase_add_test(t, ctor_clear);
    suite_add_tcase(s, t);

    t = tcase_create("push_pop");
    tcase_add_test(t, push_pop);
    tcase_add_test(t, pop_holes);
    suite_add_tcase(s, t);

    t = tcase_create("at");
    tcase_add_test(t, at);
    suite_add_tcase(s, t);

    t = tcase_create("iterators");
    tcase_add_test(t, iterators_insert);
    tcase_add_test(t, iterators_erase);
    suite_add_tcase(s, t);

    t = tcase_create("null_insertions");
    tcase_add_test(t, null_insertions_0);
    tcase_add_test(t, null_insertions_1);
    suite_add_tcase(s, t);

    t = tcase_create("random_access");
    tcase_add_test(t, random_access);
    suite_add_tcase(s, t);

    t = tcase_create("find");
    tcase_add_test(t, find);
    suite_add_tcase(s, t);

    t = tcase_create("random");
    tcase_add_test(t, random_test);
    suite_add_tcase(s, t);

    return s;
}
