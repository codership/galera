/*
 * Copyright (C) 2008-2014 Codership Oy <info@codership.com>
 *
 * $Id$
 */
/*
 *  Dummy backend specification
 */

#ifndef _gcs_dummy_h_
#define _gcs_dummy_h_

#include "gcs_backend.hpp"
#include "gcs_comp_msg.hpp"

extern GCS_BACKEND_REGISTER_FN (gcs_dummy_register);

extern GCS_BACKEND_CREATE_FN (gcs_dummy_create);

#ifdef GCS_DUMMY_TESTING

/*
 * What follows is an API for unit testing
 */

/*! Injects a message in the message queue to produce a desired msg sequence. */
extern long
gcs_dummy_inject_msg (gcs_backend_t* backend,
                      const void*    msg,
                      size_t         len,
                      gcs_msg_type_t type,
                      long           sender_idx);

/*! Sets the new component view.
 *  The same component message should be injected in the queue separately
 *  (see gcs_dummy_inject_msg()) in order to model different race conditions */
extern long
gcs_dummy_set_component (gcs_backend_t*        backend,
                         const gcs_comp_msg_t* comp);

/*! Is needed to set transitional state */
extern long
gcs_dummy_set_transitional (gcs_backend_t* backend);

#endif /* GCS_DUMMY_TESTING */

#endif /* _gcs_dummy_h_ */
