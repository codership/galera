/*
 * Copyright (C) 2008 Codership Oy <info@codership.com>
 *
 * $Id$
 */

/* Logging options */
#include <galerautils.h>

#include "gcs.hpp"

long gcs_conf_set_log_file     (FILE *file)
{ return gu_conf_set_log_file (file); }
long gcs_conf_set_log_callback (void (*logger) (int, const char*))
{ return gu_conf_set_log_callback (logger); }
long gcs_conf_self_tstamp_on   ()
{ return gu_conf_self_tstamp_on (); }
long gcs_conf_self_tstamp_off  ()
{ return gu_conf_self_tstamp_off (); }
long gcs_conf_debug_on  ()
{ return gu_conf_debug_on (); }
long gcs_conf_debug_off ()
{ return gu_conf_debug_off (); }

