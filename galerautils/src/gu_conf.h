// Copyright (C) 2007 Codership Oy <info@codership.com>
 
/**
 * @file
 * Configuration interface for libgalerautils
 *
 * $Id$
 */

#ifndef _gu_conf_h_
#define _gu_conf_h_

/* Logging options */
#include <stdio.h>
#include "gu_log.h"
int gu_conf_set_log_file     (FILE* file);
int gu_conf_set_log_callback (gu_log_cb_t callback);
int gu_conf_self_tstamp_on   ();
int gu_conf_self_tstamp_off  ();
int gu_conf_debug_on         ();
int gu_conf_debug_off        ();

#endif // _gu_conf_h_
