/* config/config.h.  Generated from config.h.in by configure.  */
/* config/config.h.in.  Generated from configure.ac by autoheader.  */

/* Define if you have clock_gettime() declared in <time.h> */
#define HAVE_CLOCK_GETTIME 1

/* Define to 1 if you have the declaration of `O_SYNC', and to 0 if you don't.
   */
#define HAVE_DECL_O_SYNC 1

/* Define to 1 if you have the declaration of `SHM_HUGETLB', and to 0 if you
   don't. */
#define HAVE_DECL_SHM_HUGETLB 1

/* Define to 1 if you have the declaration of `strerror_r', and to 0 if you
   don't. */
#define HAVE_DECL_STRERROR_R 1

/* Define to 1 if you have the <dlfcn.h> header file. */
#define HAVE_DLFCN_H 1

/* Define to 1 if you have the <errno.h> header file. */
#define HAVE_ERRNO_H 1

/* Define to 1 if you have the <fcntl.h> header file. */
#define HAVE_FCNTL_H 1

/* Define to 1 if you have the `fdatasync' function. */
#define HAVE_FDATASYNC 1

/* Define to 1 if you have the `getpagesize' function. */
#define HAVE_GETPAGESIZE 1

/* Define to 1 if you have the `gettimeofday' function. */
#define HAVE_GETTIMEOFDAY 1

/* Define to 1 if you have the <inttypes.h> header file. */
#define HAVE_INTTYPES_H 1

/* Define if you have large pages support */
#define HAVE_LARGE_PAGES 1

/* Define to 1 if you have the `aio' library (-laio). */
/* #undef HAVE_LIBAIO */

/* Define to 1 if your system has <libaio.h> header file */
/* #undef HAVE_LIBAIO_H */

/* Define to 1 if you have the `m' library (-lm). */
#define HAVE_LIBM 1

/* Define to 1 if your system has a GNU libc compatible `malloc' function, and
   to 0 otherwise. */
#define HAVE_MALLOC 1

/* Define to 1 if you have the <math.h> header file. */
#define HAVE_MATH_H 1

/* Define to 1 if you have the `memalign' function. */
#define HAVE_MEMALIGN 1

/* Define to 1 if you have the <memory.h> header file. */
#define HAVE_MEMORY_H 1

/* Define to 1 if you have the `memset' function. */
#define HAVE_MEMSET 1

/* Define to 1 if you have a working `mmap' system call. */
#define HAVE_MMAP 1

/* Define to 1 if libaio has older getevents() interface */
/* #undef HAVE_OLD_GETEVENTS */

/* Define to 1 if you have the `posix_memalign' function. */
#define HAVE_POSIX_MEMALIGN 1

/* Define if you have POSIX threads libraries and header files. */
#define HAVE_PTHREAD 1

/* Define to 1 if you have the <pthread.h> header file. */
#define HAVE_PTHREAD_H 1

/* Define to 1 if you have the `pthread_yield' function. */
#define HAVE_PTHREAD_YIELD 1

/* Define to 1 if you have the <sched.h> header file. */
#define HAVE_SCHED_H 1

/* Define to 1 if you have the <signal.h> header file. */
#define HAVE_SIGNAL_H 1

/* Define to 1 if you have the `sqrt' function. */
#define HAVE_SQRT 1

/* Define to 1 if you have the <stdint.h> header file. */
#define HAVE_STDINT_H 1

/* Define to 1 if you have the <stdlib.h> header file. */
#define HAVE_STDLIB_H 1

/* Define to 1 if you have the `strdup' function. */
#define HAVE_STRDUP 1

/* Define to 1 if you have the `strerror_r' function. */
#define HAVE_STRERROR_R 1

/* Define to 1 if you have the <strings.h> header file. */
#define HAVE_STRINGS_H 1

/* Define to 1 if you have the <string.h> header file. */
#define HAVE_STRING_H 1

/* Define to 1 if you have the <sys/aio.h> header file. */
/* #undef HAVE_SYS_AIO_H */

/* Define to 1 if you have the <sys/ipc.h> header file. */
#define HAVE_SYS_IPC_H 1

/* Define to 1 if you have the <sys/mman.h> header file. */
#define HAVE_SYS_MMAN_H 1

/* Define to 1 if you have the <sys/shm.h> header file. */
#define HAVE_SYS_SHM_H 1

/* Define to 1 if you have the <sys/stat.h> header file. */
#define HAVE_SYS_STAT_H 1

/* Define to 1 if you have the <sys/time.h> header file. */
#define HAVE_SYS_TIME_H 1

/* Define to 1 if you have the <sys/types.h> header file. */
#define HAVE_SYS_TYPES_H 1

/* Define to 1 if you have the <thread.h> header file. */
/* #undef HAVE_THREAD_H */

/* Define to 1 if you have the `thr_setconcurrency' function. */
/* #undef HAVE_THR_SETCONCURRENCY */

/* Define to 1 if you have the <unistd.h> header file. */
#define HAVE_UNISTD_H 1

/* Define to 1 if you have the `valloc' function. */
#define HAVE_VALLOC 1

/* Define if /proc/meminfo shows the huge page size (Linux only) */
#define HUGETLB_USE_PROC_MEMINFO 1

/* Define to the appropriate value for O_SYNC on your platform */
/* #undef O_SYNC */

/* Name of package */
#define PACKAGE "sysbench"

/* Define to the address where bug reports for this package should be sent. */
#define PACKAGE_BUGREPORT "alexeyk@mysql.com"

/* Define to the full name of this package. */
#define PACKAGE_NAME "sysbench"

/* Define to the full name and version of this package. */
#define PACKAGE_STRING "sysbench 0.4.8"

/* Define to the one symbol short name of this package. */
#define PACKAGE_TARNAME "sysbench"

/* Define to the version of this package. */
#define PACKAGE_VERSION "0.4.8"

/* Define to necessary symbol if this constant uses a non-standard name on
   your system. */
/* #undef PTHREAD_CREATE_JOINABLE */

/* The size of a `size_t', as computed by sizeof. */
#define SIZEOF_SIZE_T 4

/* Define to 1 if you have the ANSI C header files. */
#define STDC_HEADERS 1

/* Define to 1 if strerror_r returns char *. */
/* #undef STRERROR_R_CHAR_P */

/* Define to 1 if you can safely include both <sys/time.h> and <time.h>. */
#define TIME_WITH_SYS_TIME 1

/* Define to 1 if you want to compile with MySQL support */
#define USE_MYSQL 1

/* Define to 1 if you want to compile with Oracle support */
/* #undef USE_ORACLE */

/* Define to 1 if you want to compile with PostgreSQL support */
/* #undef USE_PGSQL */

/* Version number of package */
#define VERSION "0.4.8"

/* Number of bits in a file offset, on hosts where this is settable. */
#define _FILE_OFFSET_BITS 64

/* Define for large files, on AIX-style hosts. */
/* #undef _LARGE_FILES */

/* Define to `__inline__' or `__inline' if that's what the C compiler
   calls it, or to nothing if 'inline' is not supported under any name.  */
#ifndef __cplusplus
/* #undef inline */
#endif

/* Define to rpl_malloc if the replacement function should be used. */
/* #undef malloc */

/* Define to `long' if <sys/types.h> does not define. */
/* #undef off_t */

/* Define to `unsigned' if <sys/types.h> does not define. */
/* #undef size_t */
