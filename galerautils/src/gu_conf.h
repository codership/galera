// Copyright (C) 2007 Codership Oy <info@codership.com>
 
/**
 * @file
 * Configuration interface for libgalerautils
 *
 * $Id$
 */

#ifndef _gu_conf_h_
#define _gu_conf_h_

#ifdef __cplusplus
extern "C" {
#endif

/* Logging options */
#include <stdio.h>
#include "gu_log.h"
extern int gu_conf_set_log_file     (FILE* file);
extern int gu_conf_set_log_callback (gu_log_cb_t callback);
extern int gu_conf_self_tstamp_on   ();
extern int gu_conf_self_tstamp_off  ();
extern int gu_conf_debug_on         ();
extern int gu_conf_debug_off        ();

#ifdef __cplusplus
}
#endif

#endif // _gu_conf_h_
