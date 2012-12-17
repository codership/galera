// Copyright (C) 2012 Codership Oy <info@codership.com>

#ifndef GU_BACKTRACE_HPP
#define GU_BACKTRACE_HPP

#include "gu_backtrace.h"

#include <cstdlib>
#include <ostream>

namespace gu
{
    /*!
     * Utility class to print backtraces.
     */
    class Backtrace
    {
    public:
        /*!
         * Construct backtrace object.
         *
         * @param Maximum number of backtrace symbols resolved (default 50).
         */
        Backtrace(int size = 50)
            :
            symbols_size_(size),
            symbols_(gu_backtrace(&symbols_size_))
        { }

        ~Backtrace()
        {
            free(symbols_);
        }

        /*!
         * Print backtrace into ostream.
         *
         * @param os    Ostream to print backtrace into.
         * @param delim Delimiter separating backtrace symbols.
         */
        void print(std::ostream& os, char delim = '\n')
        {
            if (symbols_ != 0)
            {
                for (int i(0); i < symbols_size_; ++i)
                {
                    os << symbols_[i] << delim;
                }
            }
            else
            {
                os << "no backtrace available";
            }
        }

    private:
        Backtrace(const Backtrace&);
        void operator=(const Backtrace&);
        int symbols_size_;
        char** symbols_;
    };
}


#endif // GU_BACKTRACE_HPP
