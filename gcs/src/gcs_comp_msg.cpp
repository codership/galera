/*
 * Copyright (C) 2008 Codership Oy <info@codership.com>
 *
 * $Id$
 */
/*
 * Interface to membership messages - implementation
 *
 */

#include <string.h>
#include <errno.h>
#include <stddef.h>
#include <galerautils.h>

#define   GCS_COMP_MSG_ACCESS
#include "gcs_comp_msg.hpp"

static inline long
comp_msg_size (long memb_num)
{ return (sizeof(gcs_comp_msg_t) + memb_num * sizeof(gcs_comp_memb_t)); }

/*! Allocates membership object and zeroes it */
gcs_comp_msg_t*
gcs_comp_msg_new (bool prim, bool bootstrap, long my_idx, long memb_num)
{
    gcs_comp_msg_t* ret;

    assert ((memb_num > 0 && my_idx >= 0) || (memb_num == 0 && my_idx == -1));

    ret = static_cast<gcs_comp_msg_t*>(gu_calloc (1, comp_msg_size(memb_num)));

    if (NULL != ret) {
        ret->primary   = prim;
        ret->bootstrap = bootstrap;
        ret->my_idx    = my_idx;
        ret->memb_num  = memb_num;
    }

    return ret;
}

gcs_comp_msg_t*
gcs_comp_msg_leave ()
{
    return gcs_comp_msg_new (false, false, -1, 0);
}

/*! Destroys component message */
void
gcs_comp_msg_delete (gcs_comp_msg_t* comp)
{
    gu_free (comp);
}

/*! Returns total size of the component message */
long
gcs_comp_msg_size   (const gcs_comp_msg_t* comp)
{
    assert (comp);
    return comp_msg_size (comp->memb_num);
}

/*! Adds a member to the component message
 *  Returns an index of the member or negative error code */
long
gcs_comp_msg_add (gcs_comp_msg_t* comp, const char* id)
{
    size_t           id_len;
    long             i;

    assert (comp);
    assert (id);

    /* check id length */
    id_len = strlen (id);
    if (!id_len) return -EINVAL;
    if (id_len > GCS_COMP_MEMB_ID_MAX_LEN) return -ENAMETOOLONG;

    long free_slot = -1;

    /* find the free id slot and check for id uniqueness */
    for (i = 0; i < comp->memb_num; i++) {
        if (0 == comp->memb[i].id[0] && free_slot < 0) free_slot = i;
        if (0 == strcmp (comp->memb[i].id, id)) return -ENOTUNIQ;
    }

    if (free_slot < 0) return -1;

    memcpy (comp->memb[free_slot].id, id, id_len);

    return free_slot;
}

/*! Creates a copy of the component message */
gcs_comp_msg_t*
gcs_comp_msg_copy   (const gcs_comp_msg_t* comp)
{
    size_t size         = gcs_comp_msg_size(comp);
    gcs_comp_msg_t* ret = static_cast<gcs_comp_msg_t*>(gu_malloc (size));

    if (ret) memcpy (ret, comp, size);

    return ret;
}

/*! Returns member ID by index, NULL if none */
const char*
gcs_comp_msg_id (const gcs_comp_msg_t* comp, long idx)
{
    if (0 <= idx && idx < comp->memb_num)
        return comp->memb[idx].id;
    else
        return NULL;
}

/*! Returns member index by ID, -1 if none */
long
gcs_comp_msg_idx (const gcs_comp_msg_t* comp, const char* id)
{
    size_t id_len = strlen(id);
    long   idx = comp->memb_num;

    if (id_len > 0 && id_len <= GCS_COMP_MEMB_ID_MAX_LEN)
        for (idx = 0; idx < comp->memb_num; idx++)
            if (0 == strcmp (comp->memb[idx].id, id)) break;

    if (comp->memb_num == idx)
        return -1;
    else
        return idx;
}

/*! Returns primary status of the component */
bool
gcs_comp_msg_primary (const gcs_comp_msg_t* comp)
{
    return comp->primary;
}

/*! Retruns bootstrap flag of the component */
bool
gcs_comp_msg_bootstrap(const gcs_comp_msg_t* comp)
{
    return comp->bootstrap;
}

/*! Returns our own index in the membership */
long
gcs_comp_msg_self   (const gcs_comp_msg_t* comp)
{
    return comp->my_idx;
}

/*! Returns number of members in the component */
long
gcs_comp_msg_num    (const gcs_comp_msg_t* comp)
{
    return comp->memb_num;
}

