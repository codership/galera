/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
 */

#ifndef CHECK_GCOMM_HPP
#define CHECK_GCOMM_HPP

struct Suite;

/* Tests for various common types */
Suite* types_suite();
/* Tests for utilities */
Suite* util_suite();
/* Tests for logger */
Suite* logger_suite();
/* Tests for message buffer implementations */
Suite* buffer_suite();
/* Tests for event loop */
Suite* event_suite();
/* Tests for concurrency handling (mutex, cond, thread, etc.)*/
Suite* concurrent_suite();
/* Tests for TCP transport */
Suite* tcp_suite();
/* Tests for GMcast transport */
Suite* gmcast_suite();
/* Tests for EVS transport */
Suite* evs_suite();
/* Better evs suite */
Suite* evs2_suite();
/* Tests for VS trasport */
Suite* vs_suite();
/* Tests for PC transport */
Suite* pc_suite();

#endif // CHECK_GCOMM_HPP
