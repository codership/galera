/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
 *
 * $Id$
 */

/*!
 * @file Message order type enumeration
 */

#ifndef GCOMM_ORDER_HPP
#define GCOMM_ORDER_HPP

namespace gcomm
{
    // Message order type enumeration
    enum Order
    {
        // Message will not be delivered, for protocol use only
        O_DROP       = 0, 
        // Message delivery is unreliable, for protocol use only
        O_UNRELIABLE = 1,
        // Message will be delivered in source fifo order
        O_FIFO       = 2,
        // Message will be delivered in same order on all nodes 
        // if it is delivered
        O_AGREED     = 3,
        // Message will be delivered in safe order, it is guaranteed
        // that all the nodes have received the message
        O_SAFE       = 4
    };
}

#endif // GCOMM_ORDER_HPP
