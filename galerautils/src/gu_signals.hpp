//
// Copyright (C) 2021 Codership Oy <info@codership.com>
//

#ifndef GU_SIGNALS_HPP
#define GU_SIGNALS_HPP

#include <boost/signals2.hpp>

namespace gu
{
    class Signals
    {
    public:
        enum SignalType
        {
            S_CONFIG_RELOAD_CERTIFICATE,
        };
        typedef boost::signals2::signal<void (const SignalType&)> signal_t;
        typedef signal_t::slot_type slot_type;
        typedef boost::signals2::connection signal_connection;
        static Signals& Instance();
        signal_connection connect(const slot_type &subscriber);
        void signal(const SignalType&);
        Signals(Signals const&) = delete;
        Signals(Signals&&) = delete;
        Signals& operator=(Signals const&) = delete;
        Signals& operator=(Signals &&) = delete;
    private:
        Signals()
          : signal_()
        { };
        ~Signals() = default;
        signal_t signal_;
    };

} // namespace gu

#endif // GU_SIGNALS_HPP
