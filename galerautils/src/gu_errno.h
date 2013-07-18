/*
 * Copyright (C) 2013 Codership Oy <info@codership.com>
 */

#ifndef GU_ERRNO_H
#define GU_ERRNO_H

#include <errno.h>

#if defined(__APPLE__) || defined(__FreeBSD__)
# define EBADFD           (ELAST+1) /* the largest errno + 1 */
# define EREMCHG          (ELAST+2)
# define ENOTUNIQ         (ELAST+3)
# define ERESTART         (ELAST+4)
# if defined(__FreeBSD__)
#  define ENOTRECOVERABLE (ELAST+5)
#  define ENODATA         (ELAST+6)
# endif
#endif

#endif /* GU_STR_H */
