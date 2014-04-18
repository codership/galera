/*
 * Copyright (C) 2014 Codership Oy <info@codership.com>
 */

#ifndef GU_ERRNO_H
#define GU_ERRNO_H

#include <errno.h>

#if defined(__APPLE__) || defined(__FreeBSD__)
#  define GU_ELAST ELAST
#else
/* must be high enough to not collide with system errnos but lower than 256 */
#  define GU_ELAST 200
#endif

#ifndef EBADFD
#  define EBADFD          (GU_ELAST+1)
#endif
#ifndef EREMCHG
#  define EREMCHG         (GU_ELAST+2)
#endif
#ifndef ENOTUNIQ
#  define ENOTUNIQ        (GU_ELAST+3)
#endif
#ifndef ERESTART
#  define ERESTART        (GU_ELAST+4)
#endif
#ifndef ENOTRECOVERABLE
#  define ENOTRECOVERABLE (GU_ELAST+5)
#endif
#ifndef ENODATA
#  define ENODATA         (GU_ELAST+6)
#endif

#endif /* GU_STR_H */
