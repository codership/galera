//
// Copyright (C) 2010-2013 Codership Oy <info@codership.com>
//

#ifndef GALERA_ACTION_SOURCE_HPP
#define GALERA_ACTION_SOURCE_HPP

namespace galera
{
    class ActionSource
    {
    public:
        ActionSource() { }
        virtual ~ActionSource() { }
        virtual ssize_t process(void* ctx, bool& exit_loop) = 0;
        virtual long long received()       const = 0;
        virtual long long received_bytes() const = 0;
    };
}

#endif // GALERA_ACTION_SOURCE_HPP
