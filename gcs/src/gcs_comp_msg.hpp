/*
 * Copyright (C) 2008-2013 Codership Oy <info@codership.com>
 *
 * $Id$
 */
/*
 * Interface to component messages
 *
 */

#ifndef _gcs_component_h_
#define _gcs_component_h_

#include <string.h>
#include <stdbool.h>

// should accommodate human readable UUID (without trailing \0)
#define GCS_COMP_MEMB_ID_MAX_LEN GU_UUID_STR_LEN

/*! members of the same segment are physically closer than the others */
typedef uint8_t gcs_segment_t;

typedef struct gcs_comp_memb
{
    char id[GCS_COMP_MEMB_ID_MAX_LEN + 1]; /// ID assigned by the backend
    gcs_segment_t segment;
}
gcs_comp_memb_t;

#ifdef GCS_COMP_MSG_ACCESS
typedef struct gcs_comp_msg
{
    int             my_idx;    /// this node's index in membership
    int             memb_num;  /// number of members in configuration
    bool            primary;   /// 1 if we have a quorum, 0 if not
    bool            bootstrap; /// 1 if primary was bootstrapped
    int             error;     /// error code
    gcs_comp_memb_t memb[1];   /// member array
}
gcs_comp_msg_t;

#else
typedef struct gcs_comp_msg gcs_comp_msg_t;
#endif

/*! Allocates new component message 
 * @param prim     whether component is primary or not
 * @param bootstrap whether prim was bootstrapped
 * @param my_idx   this node index in the membership
 * @param memb_num number of members in component
 * @param error    error code
 * @return
 *        allocated message buffer */
extern gcs_comp_msg_t*
gcs_comp_msg_new    (bool prim, bool bootstrap, int my_idx, int memb_num, int error);

/*! Standard empty "leave" component message (to be returned on shutdown) */
extern gcs_comp_msg_t*
gcs_comp_msg_leave (int error);

/*! Destroys component message */
extern void
gcs_comp_msg_delete (gcs_comp_msg_t* comp);

/*! Adds a member to the component message
 *  Returns an index of the member or negative error code:
 *  -1            when membership is full
 *  -ENOTUNIQ     when name collides with one that is in membership already
 *  -ENAMETOOLONG wnen memory allocation for new name fails */
extern int
gcs_comp_msg_add    (gcs_comp_msg_t* comp, const char* id,
                     gcs_segment_t segment);

/*! Returns total size of the component message */
extern int
gcs_comp_msg_size   (const gcs_comp_msg_t* comp);

/*! Creates a copy of the component message */
extern gcs_comp_msg_t*
gcs_comp_msg_copy   (const gcs_comp_msg_t* comp);

/*! Returns member ID by index, NULL if none */
extern const gcs_comp_memb_t*
gcs_comp_msg_member (const gcs_comp_msg_t* comp, int idx);

/*! Returns member index by ID, -1 if none */
extern int
gcs_comp_msg_idx    (const gcs_comp_msg_t* comp, const char* id);

/*! Returns primary status of the component */
extern bool
gcs_comp_msg_primary (const gcs_comp_msg_t* comp);

/*! Returns bootstrap flag */
extern bool
gcs_comp_msg_bootstrap(const gcs_comp_msg_t* comp);

/*! Returns our own idx */
extern int
gcs_comp_msg_self (const gcs_comp_msg_t* comp);

/*! Returns number of members in the component */
extern int
gcs_comp_msg_num (const gcs_comp_msg_t* comp);

/*! Returns error code of the component message */
extern int
gcs_comp_msg_error(const gcs_comp_msg_t* comp);

#endif /* _gcs_component_h_ */
