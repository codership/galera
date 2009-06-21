/*
 * Copyright (C) 2008 Codership Oy <info@codership.com>
 *
 * $Id$
 */
 
/*!
 * @file gu_to.h Public TO monitor API
 */

#ifndef _gu_to_h_
#define _gu_to_h_

#ifdef	__cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

/*! @typedef @brief Sequence number type. */
typedef int64_t gu_seqno_t;

/*! Total Order object */
typedef struct gu_to gu_to_t;

/*! @brief Creates TO object.
 * TO object can be used to serialize access to application
 * critical section using sequence number.
 *
 * @param len   A length of the waiting queue. Should be no less than the
 *              possible maximum number of threads competing for the resource,
 *              but should not be too high either. Perhaps 1024 is good enough
 *              for most applications.
 * @param seqno A starting sequence number
 *              (the first to be used by gu_to_grab()).
 * @return Pointer to TO object or NULL in case of error.
 */
extern gu_to_t* gu_to_create (int len, gu_seqno_t seqno);

/*! @brief Destroys TO object.
 *
 * @param to A pointer to TO object to be destroyed
 * @return 0 in case of success, negative code in case of error.
 *         In particular -EBUSY means the object is used by other threads.
 */
extern long gu_to_destroy (gu_to_t** to);

/*! @brief Grabs TO resource in the specified order.
 * On successful return the mutex associated with specified TO is locked.
 * Must be released gu_to_release(). @see gu_to_release
 *
 * @param to    TO resource to be acquired.
 * @param seqno The order at which TO resouce should be aquired. For any N
 *              gu_to_grab (to, N) will return exactly after
 *              gu_to_release (to, N-1).
 * @return 0 in case of success, negative code in case of error.
 *         -EAGAIN means that there are too many threads waiting for TO
 *         already. It is safe to try again later.
 *         -ECANCEL if waiter was canceled, seqno is skipped in TO
 *         -EINTR if wait was interrupted, must retry grabbing later
 */
extern long gu_to_grab (gu_to_t* to, gu_seqno_t seqno);

/*! @brief Releases TO specified resource.
 * On succesful return unlocks the mutex associated with TO.
 * TO must be previously acquired with gu_to_grab(). @see gu_to_grab
 *
 * @param to TO resource that was previously acquired with gu_to_grab().
 * @param seqno The same number with which gu_to_grab() was called.
 * @return 0 in case of success, negative code in case of error. Any error
 *         here is an application error - attempt to release TO resource
 *         out of order (not paired with gu_to_grab()).
 */
extern long gu_to_release (gu_to_t* to, gu_seqno_t seqno);

/*! @brief The last sequence number that had been used to access TO object.
 * Note that since no locks are held, it is a conservative estimation.
 * It is guaranteed however that returned seqno is no longer in use.
 *
 * @param to A pointer to TO object.
 * @return GCS sequence number. Since GCS TO sequence starts with 1, this
 *         sequence can start with 0.
 */
extern gu_seqno_t gu_to_seqno (gu_to_t* to);

/*! @brief cancels a TO monitor waiter making it return immediately
 * It is assumed that the caller is currenly holding the TO.
 * The to-be-cancelled waiter can be some later transaction but also
 * some earlier transaction. Tests have shown that the latter case 
 * can also happen.
 *
 * @param to A pointer to TO object.
 * @param seqno Seqno of the waiter object to be cancelled
 * @return 0 for success and -ERANGE, if trying to cancel an earlier
 *         transaction
 */
extern long gu_to_cancel (gu_to_t *to, gu_seqno_t seqno);


/*!
 * Self cancel to without attempting to enter critical secion
 */
extern long gu_to_self_cancel(gu_to_t *to, gu_seqno_t seqno);

/*! @brief interrupts from TO monitor waiting state.
 *  Seqno remains valid in the queue and later seqnos still need to
 *  wait for this seqno to be released.
 * 
 *  The caller can (and must) later try gu_to_grab() again or cancel
 *  the seqno with gu_to_self_cancel().
 *
 * @param to A pointer to TO object.
 * @param seqno Seqno of the waiter object to be interrupted
 * @return 0 for success and -ERANGE, if trying to interrupt an already
 *         used transaction
 */
extern long gu_to_interrupt (gu_to_t *to, gu_seqno_t seqno);
    
#ifdef	__cplusplus
}
#endif

#endif // _gu_to_h_
