// Copyright (C) 2012 Codership Oy <info@codership.com>

#ifndef GU_BACKTRACE_H
#define GU_BACKTRACE_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/*!
 * Get current backtrace. Return buffer will contain backtrace symbols if
 * available. NULL pointer is returned if getting backtrace is not supported
 * on current platform. Maximum number of frames in backtrace is passed
 * in size parameter, number of frames in returned backtrace is assigned
 * in size parameter on return.
 *
 * @param size Pointer to integer containing maximum number of frames
 *             in backtrace
 *
 * @return Allocated array of strings containing backtrace symbols
 */
char** gu_backtrace(int* size);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* GU_BACKTRACE_H */
