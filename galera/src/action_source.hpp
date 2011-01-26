//
// Copyright (C) 2010 Codership Oy <info@codership.com>
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
        virtual ssize_t process(void* ctx) = 0;
    };
}

#endif // GALERA_ACTION_SOURCE_HPP
