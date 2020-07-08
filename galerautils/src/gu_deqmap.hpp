// Copyright (C) 2020 Codership Oy <info@codership.com>

/**
 * @file A wrapper over std::deque to emulate continuous integer sequence map.
 *
 * Holes are tolerated, but take the same amount of memory as elements.
 * For that purpose target class must have a default value which is treated as
 * null. As a result, removing N elements does not always reduce the size by N.
 *
 * Insert at iterator behavior also had to be changed from that of std::deque:
 * to be consistent insert happens exactly TO the element pointed by iterator
 * so it mostly works as "update" and only adds new elements at the end()
 *
 * The implementation is optimized towards elements mostly added at the back and
 * removed from the front.
 */

#ifndef GU_DEQMAP_HPP
#define GU_DEQMAP_HPP

#include "gu_exception.hpp" // NotFound

#include <deque>
#include <utility>   // std::pair<>
#include <iterator>  // bidirectional_iterator_tag
#include <stdexcept> // std::invalid_argument
#include <sstream>
#if __cplusplus >= 201103L
#include <type_traits>
#endif

#ifdef GU_DEQMAP_CONSISTENCY_CHECKS
#include <iostream>
#include <cstdlib>
#define GU_DEQMAP_ASSERT_CONSISTENCY assert_consistency(__func__, __LINE__)
#else
#define GU_DEQMAP_ASSERT_CONSISTENCY
#endif /* GU_DEQMAP_CONSISTENCY_CHECKS */

namespace gu
{

template <typename Key, typename Val, class Alloc = std::allocator<Val> >
class DeqMap
{
    typedef std::deque<Val, Alloc> base_type;

public:

    typedef Key                                         index_type;
#if __cplusplus >= 201103L
    typedef typename std::make_signed<index_type>::type difference_type;
#else
    typedef long long                                   difference_type;
#endif

    typedef typename base_type::size_type               size_type;
    typedef typename base_type::value_type              value_type;
    typedef typename base_type::pointer                       pointer;
    typedef typename base_type::const_pointer           const_pointer;
    typedef typename base_type::reference                     reference;
    typedef typename base_type::const_reference         const_reference;
    typedef typename base_type::allocator_type          allocator_type;

    typedef typename base_type::iterator                      iterator;
    typedef typename base_type::const_iterator          const_iterator;
    typedef typename base_type::reverse_iterator              reverse_iterator;
    typedef typename base_type::const_reverse_iterator  const_reverse_iterator;

    static value_type null_value() { return value_type(); }

    /** A test for an unset element (hole) */
    static bool not_set(const_reference val) { return val == null_value(); }

    /**
     * @param begin initial index value for the map. It is required for
     *              push_back(value_type&) and push_front(value_type&)
     *              to be meaningful operations.
     */
    explicit
    DeqMap(index_type begin,
           const allocator_type& allocator = allocator_type())
        :
        base_  (allocator),
        begin_ (begin),
        end_   (begin_)
    {
        GU_DEQMAP_ASSERT_CONSISTENCY;
    }

    ~DeqMap()
    {
        GU_DEQMAP_ASSERT_CONSISTENCY;
    };

    /** total number of elements allocated (not all of them set) */
    size_type size()  const { return base_.size();  }
    bool      empty() const { return base_.empty(); }

    /**
     * @param begin initial index value for the map. See constructor.
     */
    void
    clear(index_type begin)
    {
        GU_DEQMAP_ASSERT_CONSISTENCY;
        base_.clear();
        begin_ = begin;
        end_   = begin_;
        GU_DEQMAP_ASSERT_CONSISTENCY;
    }

    index_type index_begin() const { return begin_; }
    index_type index_end()   const { return end_;   }

    index_type index_front() const { return index_begin();   }
    index_type index_back()  const { return index_end() - 1; }

    iterator begin() { return base_.begin(); }
    iterator end()   { return base_.end();   }

    const_iterator begin() const { return base_.begin(); }
    const_iterator end()   const { return base_.end();   }

    reverse_iterator rbegin() { return base_.rbegin(); }
    reverse_iterator rend()   { return base_.rend();   }

    const_reverse_iterator rbegin() const { return base_.rbegin(); }
    const_reverse_iterator rend()   const { return base_.rend();   }

    const_reference front() const { return base_.front(); }

    const_reference back() const { return base_.back(); }

    const_reference operator[] (index_type i) const { return base_[i - begin_]; }

    const_reference
    at(index_type i) const
    {
        if (begin_ <= i && i < end_)
        {
            const_reference v(operator[](i));
            if (!not_set(v)) return v;
        }

        throw NotFound();
    }

    iterator
    find(index_type i) { return find_tmpl<iterator>(*this, i); }

    const_iterator
    find(index_type i) const { return find_tmpl<const_iterator>(*this, i); }

    /* pop_front() and pop_back() are the fastest element removal operations
     * - so set them as base for the rest. */
    void
    pop_front()
    {
        do
        {
            base_.pop_front();
            ++begin_;
        }
        while (!empty() && not_set(front())); // trim front
        GU_DEQMAP_ASSERT_CONSISTENCY;
    }

    void
    pop_back()
    {
        do
        {
            base_.pop_back();
            --end_;
        }
        while (!empty() && not_set(back())); // trim back
        GU_DEQMAP_ASSERT_CONSISTENCY;
    }

    iterator
    erase(iterator position)
    {
        /* Invalid ranges for std::deque::erase() produce undefined behavior
         * so we are not checking for empty container here. */
        GU_DEQMAP_ASSERT_CONSISTENCY;

        if (begin() == position)
        {
            pop_front();
            return begin();
        }
        else if (--end() == position)
        {
            pop_back();
            return end();
        }
        else /* don't remove elements from the middle, just unset them */
        {
            return unset(position);
        }
    }

    iterator
    erase(iterator first, iterator last)
    {
        GU_DEQMAP_ASSERT_CONSISTENCY;

        if (begin() == first)
        {
            base_.erase(first, last);
            begin_ += last - first;

            if (!empty() && not_set(front())) // trim front
            {
                pop_front();
            }

            GU_DEQMAP_ASSERT_CONSISTENCY;
            return begin();
        }
        else if (base_.end() == last)
        {
            base_.erase(first, last);
            end_ -= last - first;

            if (!empty() && not_set(back())) // trim back
            {
                pop_back();
            }

            GU_DEQMAP_ASSERT_CONSISTENCY;
            return end();
        }
        else /* don't remove elements from the middle, just unset them */
        {
            while (first < last) first = unset(first);

            GU_DEQMAP_ASSERT_CONSISTENCY;
            return first;
        }
    }

    void
    erase(index_type const idx)
    {
        if (idx == begin_)
        {
            pop_front();
        }
        else if (idx == end_ - 1)
        {
            pop_back();
        }
        else
        {
            base_[idx - begin_] = null_value();
        }
    }

    /* push_front() and push_back() are the fastest element insertion operations
     * - so set them as base for the rest. */
    void
    push_front(const value_type& val)
    {
        if (!(null_value() == val))
        {
            push_front_unchecked(val);
        }
        else
        {
            throw_null_value_exception(__func__, val, index_begin() - 1);
        }
    }

    void
    push_back (const value_type& val)
    {
        if (!(null_value() == val))
        {
            push_back_unchecked(val);
        }
        else
        {
            throw_null_value_exception(__func__, val, index_end());
        }
    }

    iterator
    insert(iterator position, const value_type& val)
    {
        GU_DEQMAP_ASSERT_CONSISTENCY;

        if (null_value() == val)
        {
            throw_null_value_exception(__func__, val, index(position));
        }

        if (end() == position)
        {
            push_back_unchecked(val);
        }
        else  /* don't insert elements in the middle, just assign them */
        {
            *position = val;
        }

        GU_DEQMAP_ASSERT_CONSISTENCY;

        return position;
    }

    void
    insert(iterator position, size_type n, const value_type& val)
    {
        GU_DEQMAP_ASSERT_CONSISTENCY;

        if (null_value() == val)
        {
            throw_null_value_exception(__func__, val, index(position));
        }

        while (position != end() && n)
        {
            position = set(position, val);
            --n;
        }

        if (n)
        {
            end_ += n;
            base_.insert(position, n, val);
        }

        GU_DEQMAP_ASSERT_CONSISTENCY;
    }

    void
    insert(index_type const i, const value_type& val)
    {
        GU_DEQMAP_ASSERT_CONSISTENCY;

        if (null_value() == val)
        {
            throw_null_value_exception(__func__, val, i);
        }

        if (begin_ != end_)
        {
            if (i >= end_)
            {
                if (i == end_)
                {
                    push_back_unchecked(val);
                }
                else
                {
                    size_type const off(i - end_ + 1);
                    base_.insert(end(), off, null_value());
                    end_ += off;
                    base_.back() = val;
                }
            }
            else if (i < begin_)
            {
                if (i + 1 == begin_)
                {
                    push_front_unchecked(val);
                }
                else
                {
                    size_type const off(begin_ - i);
                    base_.insert(begin(), off, null_value());
                    begin_ = i;
                    base_.front() = val;
                }
            }
            else
            {
                base_[i - begin_] = val;
            }
        }
        else
        {
            begin_ = end_ = i;
            push_back_unchecked(val);
        }

        GU_DEQMAP_ASSERT_CONSISTENCY;
    }

    index_type
    index(const_iterator it) const
    {
        GU_DEQMAP_ASSERT_CONSISTENCY;

        return (it - base_.begin()) + begin_;
    }

    index_type
    index(const_reverse_iterator it) const
    {
        GU_DEQMAP_ASSERT_CONSISTENCY;

        return end_ - (it - base_.rbegin()) - 1;
    }

    /**
     * This is a port of std::map::upper_bound(): it returns the index of the
     * first *set* element in container that is supposed to go after i. Unset
     * elements are treated as absent.
     */
    index_type
    upper_bound(index_type i) const
    {
        GU_DEQMAP_ASSERT_CONSISTENCY;

        if (i >= end_)
        {
            return end_;
        }

        if (i >= begin_)
        {
            do
            {
                ++i;
            }
            while (i < end_ && not_set(operator[](i)));

            return i;
        }

        return begin_;
    }

    void
    print(std::ostream& os) const
    {
        os << "gu::DeqMap(size: " << size() << ", begin: " << +index_begin()
           << ", end: " << +index_end();
        os << ", front: ";
        size() ? os << +front() : os << "n/a";
        os << ", back: ";
        size() ? os << +back()  : os << "n/a";
        os << ')';
    }

private:

    base_type  base_;
    index_type begin_;
    index_type end_;

    iterator&
    unset(iterator& it)
    {
        *it = null_value();
        return ++it;
    }

    iterator&
    set(iterator& it, const value_type& val)
    {
        *it = val;
        return ++it;
    }

    void
    push_front_unchecked(const value_type& val)
    {
        base_.push_front(val);
        --begin_;
    }

    void
    push_back_unchecked(const value_type& val)
    {
        base_.push_back(val);
        ++end_;
    }

    void
    throw_null_value_exception(const char* const func_name,
                               const value_type& val,
                               const index_type& pos)
    {
        std::ostringstream what;

        what << "Null value '" << val << "' with index " << pos
             << " was passed to " << func_name;

        throw std::invalid_argument(what.str());
    }

    /* Template to avoid code duplication in find() methods */
    template <typename Iter, typename This>
    static Iter
    find_tmpl (This& t, index_type i)
    {
#ifdef GU_DEQMAP_CONSISTENCY_CHECKS
        t.GU_DEQMAP_ASSERT_CONSISTENCY;
#endif
        if (i >= t.begin_ && i < t.end_)
            return t.begin() += (i - t.begin_);
        else
            return t.end();
    }

#ifdef GU_DEQMAP_CONSISTENCY_CHECKS
    void
    assert_consistency(const char* const func_name, int const line) const
    {
        bool ok(true);
        int check(1);

        ok = ok && (begin() + size() == end()); check += ok;
        ok = ok && (index_begin() + index_type(size()) == index_end());
        check += ok;

        if (!empty())
        {
            ok = ok && (!(front() == null_value())); check += ok;
            ok = ok && (!(back()  == null_value())); check += ok;

            ok = ok && (operator[](index_begin()) == front());  check += ok;
            ok = ok && (operator[](index_end() - 1) == back()); check += ok;

            ok = ok && (*begin()  == front()); check += ok;
            ok = ok && (*(--end()) == back()); check += ok;

            if (size() == 1)
            {
                ok = ok && (front()  == back());    check += ok;
                ok = ok && (*begin() == *rbegin()); check += ok;
            }
        }
        else
        {
            ok = ok && (begin() == end()); check += ok;
        }

        if (!ok)
        {
            std::cerr << "gu::DeqMap consistency check " << check
                      << " failed at " << func_name
                      << "():" << line << " map: ";
            print(std::cerr);
            std::cerr << std::endl;

            abort();
        }
    }
#endif /* GU_DEQMAP_CONSISTENCY_CHECKS */

}; /* class DeqMap */

template <typename K, typename V, typename A>
static inline std::ostream&
operator<<(std::ostream& os, const DeqMap<K, V, A>& m)
{
    m.print(os);
    return os;
}

} /* namespace gu */

#endif /* GU_DEQMAP_HPP */
