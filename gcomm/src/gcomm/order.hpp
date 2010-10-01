/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
 *
 * $Id$
 */

/*!
 * @file order.hpp
 *
 * @brief Message order type enumeration.
 */

#ifndef GCOMM_ORDER_HPP
#define GCOMM_ORDER_HPP

namespace gcomm
{
    /*!
     * @brief Message order type enumeration.
     */
    enum Order
    {
        /*! Message will not be delivered, for protocol use only. */
        O_DROP       = 0,
        /*! Message delivery is unreliable, for protocol use only. */
        O_UNRELIABLE = 1,
        /*! Message will be delivered in source fifo order. */
        O_FIFO       = 2,
        /*!
         * Message will be delivered in same order on all nodes
         * if it is delivered.
         */
        O_AGREED     = 3,
        /*!
         * Message will be delivered in safe order, it is guaranteed
         * that all the nodes in group have received the message.
         */
        O_SAFE       = 4,

        /*!
         * Message will be delivered only locally and delivery will fulfill the
         * following property:
         *
         * Let M_c be message tagged with O_LOCAL_CAUSAL ordering requirement.
         * Any message M_a which is delivered on any node so that delivery
         * has causal precedence on generating M_c will be delivered locally
         * before M_c.
         *
         * Note that the causality is guaranteed only with respect to
         * already delivered messages.
         */
        O_LOCAL_CAUSAL = 8
    };
}

#endif // GCOMM_ORDER_HPP
