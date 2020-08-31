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

    bool
    operator !=(const Test& other) const { return !operator==(other); }

    int
    operator +() const { return val_; }

private:
    int val_;
};

static std::ostream&
operator <<(std::ostream& os, const Test& t) { os << +t; return os; }

START_TEST(ctor_clear)
{
    typedef gu::DeqMap<signed char, Test> Map;
    Map m(-1);

    ck_assert(m.size() <= 0);
    ck_assert(m.index_begin() == Map::index_type(-1));
    ck_assert(m.index_end() == Map::index_type(-1));

    ck_assert(m.upper_bound(0) == m.index_end());

    m.clear(5);

    ck_assert(m.size() <= 0);
    ck_assert(m.index_begin() == Map::index_type(5));
    ck_assert(m.index_end() == Map::index_type(5));

    ck_assert(m.upper_bound(0) == m.index_begin());
}
END_TEST

START_TEST(push_pop)
{
    typedef gu::DeqMap<signed char, signed char> Map;
    Map m(-1);

    /* some push acton */
    m.push_back(1); // -1
    m.push_back(2); //  0
    m.push_back(3); //  1
    m.push_front(4);// -2
    /* Here we have: 4, 1, 2, 3 */

    ck_assert(m.size()  == 4);
    ck_assert(m.front() == 4);
    ck_assert(m.back()  == 3);
    ck_assert(m.index_begin() == Map::index_type(-2));
    ck_assert(m.index_end()   == Map::index_type(2));

    m.pop_front();
    /* Here we have: 1, 2, 3 */

    ck_assert(m.size()  == 3);
    ck_assert(m.front() == 1);
    ck_assert(m.back()  == 3);
    ck_assert(m.index_begin() == Map::index_type(-1));
    ck_assert(m.index_end()   == Map::index_type(2));

    m.pop_back();
    /* Here we have: 1, 2 */

    ck_assert(m.size()  == 2);
    ck_assert(m.front() == 1);
    ck_assert(m.back()  == 2);
    ck_assert(m.index_begin() == Map::index_type(-1));
    ck_assert(m.index_end()   == Map::index_type(1));

    m.pop_front();
    m.pop_front();
    /* Here we have: empty */

    ck_assert(m.size()  == 0);
    ck_assert(m.index_begin() == Map::index_type(1));
    ck_assert(m.index_end()   == Map::index_type(1));

    m.push_front(7); // 0

    ck_assert(m.size()  == 1);
    ck_assert(m.front() == 7);
    ck_assert(m.back()  == 7);
    ck_assert(m.index_begin() == Map::index_type(0));
    ck_assert(m.index_end()   == Map::index_type(1));

    m.pop_back();

    ck_assert(m.size()  == 0);
    ck_assert(m.index_begin() == Map::index_type(0));
    ck_assert(m.index_end()   == Map::index_type(0));
}
END_TEST

START_TEST(pop_holes) /* autoshrinking when popping on container with holes */
{
    typedef gu::DeqMap<signed char, signed char> Map;
    Map m(-1);

    ck_assert(m.size() == 0);
    ck_assert(m.index_begin() == Map::index_type(-1));
    ck_assert(m.index_end()   == Map::index_type(-1));
    ck_assert(m.index_back()  == Map::index_type(-2));

    m.insert(1, 1);
    m.insert(4, 4);

    ck_assert(m.size() == 4);
    ck_assert(m.index_begin() == Map::index_type(1));
    ck_assert(m.index_end()   == Map::index_type(5));
    ck_assert(m.index_back()  == Map::index_type(4));
    ck_assert(Map::not_set(m[2]));
    ck_assert(Map::not_set(m[3]));

    m.pop_front();

    ck_assert(m.size() == 1);
    ck_assert(m.index_begin() == Map::index_type(4));
    ck_assert(m.index_end()   == Map::index_type(5));
    ck_assert(m.index_back()  == Map::index_type(4));
    ck_assert(*m.begin() == 4);

    m.insert(1, 1);

    ck_assert(m.size() == 4);
    ck_assert(m.index_begin() == Map::index_type(1));
    ck_assert(m.index_end()   == Map::index_type(5));
    ck_assert(m.index_back()  == Map::index_type(4));
    ck_assert(Map::not_set(m[2]));
    ck_assert(Map::not_set(m[3]));

    m.pop_back();

    ck_assert(m.size() == 1);
    ck_assert(m.index_begin() == Map::index_type(1));
    ck_assert(m.index_end()   == Map::index_type(2));
    ck_assert(m.index_back()  == Map::index_type(1));
    ck_assert(*m.begin() == 1);
}
END_TEST

START_TEST(at)
{
    typedef gu::DeqMap<signed char, signed char> Map;
    Map m(-1);

    try
    {
        m.at(-1);
        ck_abort_msg("expected exception");
    }
    catch (gu::NotFound&) {}

    m.push_back(3);

    try
    {
        ck_assert(3 == m.at(-1));
    }
    catch (...)
    {
        ck_abort_msg("unexpected exception");
    }

    try
    {
        m.at(-2);
        ck_abort_msg("expected exception");
    }
    catch (gu::NotFound&) {}

    try
    {
        m.at(0);
        ck_abort_msg("expected exception");
    }
    catch (gu::NotFound&) {}
}
END_TEST

START_TEST(iterators_insert)
{
    typedef gu::DeqMap<signed char, signed char> Map;
    Map m(-1);

    m.insert(m.begin(), 4, 4);
    /* here we have 4, 4, 4, 4 */

    ck_assert(m.size() == 4);
    ck_assert(*m.begin()  == 4);
    ck_assert(*m.rbegin() == 4);
    ck_assert(m.index_begin() == Map::index_type(-1));
    ck_assert(m.index_end()   == Map::index_type(3));

    m.insert(m.begin() + 1, 2, 2); // bulk insert (overwrite) in the middle
    /* here we have 4, 2, 2, 4 */

    ck_assert(m.size() == 4);
    ck_assert(*m.begin()  == 4);
    ck_assert(*m.rbegin() == 4);
    ck_assert(m.index_begin() == Map::index_type(-1));
    ck_assert(m.index_end()   == Map::index_type(3));

    m.insert(m.begin(), 2, 1); // bulk insert (overwrite) in the beginning
    /* here we have 1, 1, 2, 4 */

    ck_assert(m.size() == 4);
    ck_assert(*m.begin()  == 1);
    ck_assert(*m.rbegin() == 4);
    ck_assert(m.index_begin() == Map::index_type(-1));
    ck_assert(m.index_end()   == Map::index_type(3));

    m.insert(m.end(), 2, 5); // bulk insert in the end
    /* here we have 1, 1, 2, 4, 5, 5 */

    ck_assert(m.size() == 6);
    ck_assert(*m.begin()  == 1);
    ck_assert(*m.rbegin() == 5);
    ck_assert(m.index_begin() == Map::index_type(-1));
    ck_assert(m.index_end()   == Map::index_type(5));

    m.insert(m.begin(), -1); // single insert (overwrite) in the beginning
    /* here we have -1, 1, 2, 4, 5, 5 */

    ck_assert(m.size() == 6);
    ck_assert(*m.begin()  == -1);
    ck_assert(*m.rbegin() == 5);
    ck_assert(m.index_begin() == Map::index_type(-1));
    ck_assert(m.index_end()   == Map::index_type(5));

    Map::iterator b(m.begin());
    ++b;
    ck_assert(!Map::not_set(*b));
    ck_assert(*b == 1);
    ck_assert(m.index(b) == 0);
    ++b;
    ck_assert(*b == 2);
    ck_assert(m.index(b) == 1);

    m.insert(b, 1); // single insert (overwrite) in the middle
    /* here we have -1, 1, 1, 4, 5, 5 */

    ck_assert(m.size() == 6);
    ck_assert(*b == 1);

    m.insert(m.end(), 6); // single insert in the end
    /* here we have -1, 1, 1, 4, 5, 5, 6 */

    ck_assert(m.size() == 7);
    ck_assert(*m.begin()  == -1);
    ck_assert(*m.rbegin() == 6);
    ck_assert(m.index_begin() == Map::index_type(-1));
    ck_assert(m.index_end()   == Map::index_type(6));
    ck_assert(m.index_back()  == Map::index_type(5));
    ck_assert(m[m.index_back()] == 6);

    ++b;
    ck_assert(*b == 4);
    ck_assert(m.index(b) == 2);
    *b = 2; // assignment via iterator
    /* here we have -1, 1, 1, 2, 5, 5, 6 */

    ck_assert(m.size() == 7);
    ck_assert(*b == 2);

    Map::reverse_iterator rb(m.rbegin());
    ck_assert(*rb == 6);
    ck_assert(m.index(rb) == 5);

    *rb = 5;
    /* here we have -1, 1, 1, 2, 5, 5, 5 */
    ck_assert(*rb == 5);

    ++rb;
    ck_assert(*rb == 5);
    ck_assert(m.index(rb) == 4);

    *rb = 4;
    /* here we have -1, 1, 1, 2, 5, 4, 5 */
    ck_assert(*rb == 4);
}
END_TEST

START_TEST(iterators_erase)
{
    typedef gu::DeqMap<signed char, signed char> Map;
    Map m(-1);

    Map::size_type init_size(12);
    m.insert(m.begin(), init_size, 1);
    /* here we have 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 */

    ck_assert(m.size() == init_size);
    ck_assert(m.index_begin() == Map::index_type(-1));
    ck_assert(m.index_end() == m.index_begin() + int(m.size()));
    ck_assert(m.front() == 1);
    ck_assert(m.back()  == 1);

    Map::iterator b(m.begin() + 2);
    m.erase(b); // single erase in the middle
    /* here we have 1, 1, N, 1, 1, 1, 1, 1, 1, 1, 1, 1 */
    /*                    b                            */
    ck_assert(Map::not_set(*b));

    Map::iterator e(m.end() - 4);
    m.erase(e, e + 2); // bulk erase in the middle
    /* here we have 1, 1, N, 1, 1, 1, 1, 1, N, N, 1, 1 */
    /*                    b                 e          */
    ck_assert(Map::not_set(*e));

    ck_assert(m.size() == init_size);
    ck_assert(m.index_begin() == Map::index_type(-1));
    ck_assert(m.index_end() == m.index_begin() + int(m.size()));
    ck_assert(m.front() == 1);
    ck_assert(m.back()  == 1);

    m.erase(m.begin()); // single erase at the front
    /* here we have    1, N, 1, 1, 1, 1, 1, N, N, 1, 1 */
    /*                    b                 e          */

    ck_assert(m.size() == init_size - 1);
    ck_assert(m.index_begin() == Map::index_type(0));
    ck_assert(m.index_end() == m.index_begin() + int(m.size()));
    ck_assert(m.front() == 1);
    ck_assert(m.back()  == 1);

    m.erase(m.begin()); // single erase at the front before hole
    /* here we have          1, 1, 1, 1, 1, N, N, 1, 1 */
    /*                    b                 e          */

    ck_assert(m.size() == init_size - 3);
    ck_assert(m.index_begin() == Map::index_type(2));
    ck_assert(m.index_end() == m.index_begin() + int(m.size()));
    ck_assert(m.front() == 1);
    ck_assert(m.back()  == 1);

    m.erase(m.end() - 1); // single erase at the back
    /* here we have          1, 1, 1, 1, 1, N, N, 1    */
    /*                    b                 e          */

    ck_assert(m.size() == init_size - 4);
    ck_assert(m.index_begin() == Map::index_type(2));
    ck_assert(m.index_end() == m.index_begin() + int(m.size()));
    ck_assert(m.front() == 1);
    ck_assert(m.back()  == 1);

    m.erase(m.end() - 1); // single erase at the back before hole
    /* here we have          1, 1, 1, 1, 1             */
    /*                    b                 e          */

    ck_assert(m.size() == init_size - 7);
    ck_assert(m.index_begin() == Map::index_type(2));
    ck_assert(m.index_end() == m.index_begin() + int(m.size()));
    ck_assert(m.front() == 1);
    ck_assert(m.back()  == 1);

    m.erase(m.begin(), m.begin() + 2); // bulk erase at the front
    /* here we have                1, 1, 1             */
    /*                    b                 e          */

    ck_assert(m.size() == init_size - 9);
    ck_assert(m.index_begin() == Map::index_type(4));
    ck_assert(m.index_end() == m.index_begin() + int(m.size()));
    ck_assert(m.front() == 1);
    ck_assert(m.back()  == 1);

    m.erase(m.end() - 2, m.end()); // bulk erase at the end
    /* here we have                1                   */
    /*                    b                 e          */

    ck_assert(m.size() == init_size - 11);
    ck_assert(m.index_begin() == Map::index_type(4));
    ck_assert(m.index_end() == m.index_begin() + int(m.size()));
    ck_assert(m.front() == 1);
    ck_assert(m.back()  == 1);

    m.insert(m.end(), 16, 1);
    init_size = m.size();
    ck_assert (init_size == 17);
    /* here we have 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 */

    m.erase(m.begin() + 1, m.begin() + 3);
    /* here we have 1, N, N, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 */
    ck_assert(m.size() == init_size);

    m.erase(m.begin() + 2, m.begin() + 4);
    /* here we have 1, N, N, N, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 */
    ck_assert(m.size() == init_size);

    m.erase(m.begin(), m.begin() + 2);
    /* here we have             1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 */
    ck_assert(m.size() == init_size - 4);
    ck_assert(m.index_begin() == Map::index_type(8));

    m.erase(m.end() - 3, m.end() - 1);
    /* here we have             1, 1, 1, 1, 1, 1, 1, 1, 1, 1, N, N, 1 */
    ck_assert(m.size() == init_size - 4);

    m.erase(m.end() - 4, m.end() - 1);
    /* here we have             1, 1, 1, 1, 1, 1, 1, 1, 1, N, N, N, 1 */
    ck_assert(m.size() == init_size - 4);

    m.erase(m.end() - 2, m.end());
    /* here we have             1, 1, 1, 1, 1, 1, 1, 1, 1             */
    ck_assert(m.size() == init_size - 8);

    m.erase(m.begin() + 2, m.begin() + 4);
    /* here we have             1, 1, N, N, 1, 1, 1, 1, 1             */
    ck_assert(m.size() == init_size - 8);

    m.erase(m.begin() + 5, m.begin() + 7);
    /* here we have             1, 1, N, N, 1, N, N, 1, 1             */
    ck_assert(m.size() == init_size - 8);
    ck_assert(m.index_begin() == Map::index_type(8));

    ck_assert(Map::not_set(m[m.index_begin() + 2]));
    ck_assert(Map::not_set(m[m.index_begin() + 3]));
    ck_assert(!Map::not_set(m[m.index_begin() + 4]));
    ck_assert(Map::not_set(m[m.index_begin() + 5]));
    ck_assert(Map::not_set(m[m.index_begin() + 6]));

    m.erase(m.begin() + 1, m.begin() + 8);
    /* here we have             1, N, N, N, N, N, N, N, 1             */
    ck_assert(m.size() == init_size - 8);

    m.erase(m.begin());
    /* here we have                                     1             */
    ck_assert(m.size() == 1);
    ck_assert(m.index_begin() == Map::index_type(16));
}
END_TEST

/* Tests attempts to insert Null values to container of size SIZE. Two cases
 * for every insert method: beginning (equivalent to middle) and end */
static void
null_insertions(size_t const SIZE)
{
    typedef gu::DeqMap<signed char, Test> Map;
    Map::value_type const Null(Map::null_value());
    ck_assert(Map::null_value() == Null);
    ck_assert(Map::not_set(Null));

    Map::value_type const Default = Map::value_type(0);
    ck_assert(Null != Default);
    ck_assert(!Map::not_set(Default));

    Map::index_type const Begin(-1);
    Map m(Begin);

    m.insert(m.end(), SIZE, Default);

    ck_assert(m.size() == SIZE);
    ck_assert(m.index_begin() == Begin);
    ck_assert(m.index_end()   == m.index_begin() + int(m.size()));
    ck_assert((m.size() == 0 || m.front() == Default));
    ck_assert((m.size() == 0 || m.back()  == Default));

    try
    {
        m.push_front(Null);
        ck_abort_msg("No exception in push_front()");
    }
    catch (std::invalid_argument& e)
    {
        try
        {
            m.push_back(Null);
            ck_abort_msg("No exception in push_back()");
        }
        catch (std::invalid_argument& e)
        {
            ck_assert(m.size() == SIZE);
            ck_assert(m.index_begin() == Begin);
            ck_assert(m.index_end()   == m.index_begin() + int(m.size()));
            ck_assert(m.size() == 0 || m.front() == Default);
            ck_assert(m.size() == 0 || m.back()  == Default);
        }
        catch (...)
        {
            ck_abort_msg("Unexpected exception in push_back()");
        }
    }
    catch (...)
    {
        ck_abort_msg("Unexpected exception in push_front()");
    }

    try
    {
        m.insert(m.begin(), Null);
        ck_abort_msg("No exception in insert() at the begin()");
    }
    catch (std::invalid_argument& e)
    {
        try
        {
            m.insert(m.end(), Null);
            ck_abort_msg("No exception in insert() at the end()");
        }
        catch (std::invalid_argument& e)
        {
            ck_assert(m.size() == SIZE);
            ck_assert(m.index_begin() == Begin);
            ck_assert(m.index_end()   == m.index_begin() + int(m.size()));
            ck_assert(m.size() == 0 || m.front() == Default);
            ck_assert(m.size() == 0 || m.back()  == Default);
        }
        catch (...)
        {
            ck_abort_msg("Unexpected exception in insert() at the end()");
        }
    }
    catch (...)
    {
        ck_abort_msg("Unexpected exception in insert() at the begin()");
    }

    try
    {
        m.insert(m.begin(), 3, Null);
        ck_abort_msg("No exception in insert() at the begin()");
    }
    catch (std::invalid_argument& e)
    {
        try
        {
            m.insert(m.end(), 3, Null);
            ck_abort_msg("No exception in insert() at the end()");
        }
        catch (std::invalid_argument& e)
        {
            ck_assert(m.size() == SIZE);
            ck_assert(m.index_begin() == Begin);
            ck_assert(m.index_end()   == m.index_begin() + int(m.size()));
            ck_assert(m.size() == 0 || m.front() == Default);
            ck_assert(m.size() == 0 || m.back()  == Default);
        }
        catch (...)
        {
            ck_abort_msg("Unexpected exception in insert() at the end()");
        }
    }
    catch (...)
    {
        ck_abort_msg("Unexpected exception in insert() at the begin()");
    }


    try
    {
        m.insert(m.index_begin(), Null);
        ck_abort_msg("No exception in insert() at the begin()");
    }
    catch (std::invalid_argument& e)
    {
        try
        {
            m.insert(m.index_end(), Null);
            ck_abort_msg("No exception in insert() at the end()");
        }
        catch (std::invalid_argument& e)
        {
            ck_assert(m.size() == SIZE);
            ck_assert(m.index_begin() == Begin);
            ck_assert(m.index_end()   == m.index_begin() + int(m.size()));
            ck_assert(m.size() == 0 || m.front() == Default);
            ck_assert(m.size() == 0 || m.back()  == Default);
        }
        catch (...)
        {
            ck_abort_msg("Unexpected exception in insert() at the end()");
        }
    }
    catch (...)
    {
        ck_abort_msg("Unexpected exception in insert() at the begin()");
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
    typedef gu::DeqMap<signed char, Test> Map;
    Map::index_type const Min(-5);
    Map::index_type const Max(5);

    Map m(100);
    m.insert(Min, Test(Min));
    m.insert(Max, Test(Max));

    ck_assert(m.size() == size_t(Max - Min + 1));
    ck_assert(m.index_begin() == Min);
    ck_assert(m.index_back()  == Max);
    ck_assert(m.front() == Test(Min));
    ck_assert(m.back()  == Test(Max));

    for (Map::index_type i(Min + 1); i < Max; ++i)
    {
        ck_assert(Map::not_set(m[i]));
    }

    for (Map::index_type i(Min + 1); i < Max; ++i)
    {
        Map::value_type const val((Test(i)));

        if (!Map::not_set(val)) m.insert(i, val);
    }

    for (Map::index_type i(Min); i <= Max; ++i)
    {
        Map::value_type const val((Test(i)));

        ck_assert(m[i] == val);
        if (!Map::not_set(val)) ck_assert(m.at(i) == val);
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

    ck_assert(m.size() == 3);
    ck_assert(m[1] == 1);
    ck_assert(m[3] == 3);

    for(Map::index_type i(m.index_begin()); i < m.index_end(); ++i)
    {
        ck_assert(*m.find(i) == i);
        *m.find(i) = i + 1;
        ck_assert(*m.find(i) == i + 1);
    }
    const Map& mc(m); // test const overload

    for(Map::index_type i(mc.index_begin()); i < mc.index_end(); ++i)
        ck_assert(*mc.find(i) == i + 1);
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

    static int const SIZE(1<<13); // 8K
    static int const SEED(2);

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
            ck_assert(it != map.end());
            if (size_change)
                ck_assert(init_state != map_state(map));
            else
                ck_assert(init_state == map_state(map));
        }
        break;
        case INDEX:
            map.insert(idx, val);
            if (size_change)
                ck_assert(init_state != map_state(map));
            else
                ck_assert(init_state == map_state(map));

            break;
        case PUSHPOP:
            if (idx + 1 == begin)
                map.push_front(val);
            else
            {
                ck_assert(idx != end);
                map.push_back(val);
            }

            ck_assert(init_state != map_state(map));
        }
    }

    ck_assert(!map.empty());

    /* now erase all elements */
    while (!map.empty())
    {
        int const begin(map.index_begin());
        int const end(map.index_end());
        ck_assert(begin < end);
        ck_assert(map.index_back() < end);

        int const size(end - begin);
        int const idx((rand() % size) + begin);
        ck_assert(idx < map.index_end());

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
            ck_assert_msg((map[idx]%SIZE) == idx, /* see filling loop above */
                          "Expected %d, got %d %% %d = %d",
                          idx, map[idx], SIZE, map[idx]%SIZE);
        }

        map_state const init_state(map);

        switch (how)
        {
        case ITERATOR:
            map.erase(map.begin() + (idx - begin));
            if (size_change)
                ck_assert(init_state != map_state(map));
            else
                ck_assert(init_state == map_state(map));

            break;
        case INDEX:
            map.erase(idx);
            if (size_change)
                ck_assert(init_state != map_state(map));
            else
                ck_assert(init_state == map_state(map));

            break;
        case PUSHPOP:
            if (idx == begin)
                map.pop_front();
            else
                map.pop_back();

            ck_assert(init_state != map_state(map));
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
    tcase_set_timeout(t, 120);
    suite_add_tcase(s, t);

    return s;
}
