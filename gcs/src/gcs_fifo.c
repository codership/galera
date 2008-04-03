/*
 * Copyright (C) 2008 Codership Oy <info@codership.com>
 *
 * $Id$
 */
/*
 * FIFO "class"
 * Implements simple fixed size "mallocless" FIFO.
 * Except gcs_fifo_create() there are two types of fifo
 * access methods - protected and unprotected. Unprotected
 * methods assume that calling routines implement their own
 * protection, and thus are simplified for speed.
 */

#include <galerautils.h>

#include "gcs_fifo.h"

/* Creates FIFO object. Since it practically consists of array of (void*),
 * the length can be chosen arbitrarily high - to minimize the risk
 * of overflow situation.
 */
gcs_fifo_t* gcs_fifo_create (const size_t length)
{
    gcs_fifo_t* ret = NULL;

    /* check limits */
    if ((length < 1) || (length > (1L << (8 * sizeof (size_t) - 1))))
	return NULL;

    ret = GU_CALLOC (1, gcs_fifo_t);
    if (ret) {
	ret->length = 1;
	while (ret->length < length) {
	    ret->length = ret->length << 1; /*real length must be power of 2*/
	}

	ret->mask   = ret->length - 1;
	ret->fifo   = GU_CALLOC (ret->length, const void*);
	if (ret->fifo) {
	    gu_mutex_init (&ret->busy_lock, NULL);
	    gu_cond_init  (&ret->free_signal, NULL);
	    /* everything else must be initialized to 0 by calloc */
	}
	else {
	    gu_free (ret);
	    ret = NULL;
	}
    }

    return ret;
}

int gcs_fifo_destroy (gcs_fifo_t** fifo)
{
    gcs_fifo_t* f = *fifo;
    if (f) {
	gu_mutex_destroy (&f->busy_lock);
	gu_cond_destroy  (&f->free_signal);
	gu_free (f->fifo);
	gu_free (f);
	*fifo = NULL;
    }
    return 0;
}


int gcs_fifo_safe_destroy (gcs_fifo_t** fifo)
{
    gcs_fifo_t* f = *fifo;
    int ret;

    if (f) {
	if ((ret = gu_mutex_lock (&f->busy_lock))) {
	    return -ret; /* something's wrong */
	}
	if (f->destroyed) {
	    gu_mutex_unlock (&f->busy_lock);
	    return -EALREADY;
	}

	f->destroyed = 1;

	while (f->used) {
	    /* there are some items in FIFO - and that means
	     * no gcs_fifo_safe_get() is waiting on condition */
	    gu_mutex_unlock (&f->busy_lock);
	    /* let them get remaining items from FIFO,
	     * we don't know how to deallocate them ourselves.
	     * unfortunately this may take some time */
	    usleep (10000); /* sleep a bit to avoid busy loop */
	    gu_mutex_lock (&f->busy_lock);
	}
	f->length = 0;

	/* now all we have - some functions waiting for lock or signal */
	while (pthread_cond_destroy (&f->free_signal)) {
	    gu_cond_signal (&f->free_signal);
	}

	/* at this point there are only functions waiting for lock */
	gu_mutex_unlock (&f->busy_lock);
	while (gu_mutex_destroy (&f->busy_lock)) {
	    /* this should be fast provided safe get and safe put are
	     * wtitten correctly. They should immediately freak out. */
	    gu_mutex_lock   (&f->busy_lock);
	    gu_mutex_unlock (&f->busy_lock);
	}

	/* now nobody's waiting for anything */
	gu_free (f->fifo);
	gu_free (f);
	*fifo = NULL;
	return 0;
    }
    return -EINVAL;
}
