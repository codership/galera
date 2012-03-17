/******************************************************************************
 *									      *
 *				   N O T I C E				      *
 *									      *
 *		      Copyright Abandoned, 1987, Fred Fish		      *
 *									      *
 *									      *
 *	This previously copyrighted work has been placed into the  public     *
 *	domain	by  the  author  and  may be freely used for any purpose,     *
 *	private or commercial.						      *
 *									      *
 *	Because of the number of inquiries I was receiving about the  use     *
 *	of this product in commercially developed works I have decided to     *
 *	simply make it public domain to further its unrestricted use.	I     *
 *	specifically  would  be  most happy to see this material become a     *
 *	part of the standard Unix distributions by AT&T and the  Berkeley     *
 *	Computer  Science  Research Group, and a standard part of the GNU     *
 *	system from the Free Software Foundation.			      *
 *									      *
 *	I would appreciate it, as a courtesy, if this notice is  left  in     *
 *	all copies and derivative works.  Thank you.			      *
 *									      *
 *	The author makes no warranty of any kind  with	respect  to  this     *
 *	product  and  explicitly disclaims any implied warranties of mer-     *
 *	chantability or fitness for any particular purpose.		      *
 *									      *
 ******************************************************************************
 */


/*
 *  FILE
 *
 *	dbug.c	 runtime support routines for dbug package
 *
 *  SCCS
 *
 *	@(#)dbug.c	1.25	7/25/89
 *
 *  DESCRIPTION
 *
 *	These are the runtime support routines for the dbug package.
 *	The dbug package has two main components; the user include
 *	file containing various macro definitions, and the runtime
 *	support routines which are called from the macro expansions.
 *
 *	Externally visible functions in the runtime support module
 *	use the naming convention pattern "_db_xx...xx_", thus
 *	they are unlikely to collide with user defined function names.
 *
 *  AUTHOR(S)
 *
 *	Fred Fish		(base code)
 *	Enhanced Software Technologies, Tempe, AZ
 *	asuvax!mcdphx!estinc!fnf
 *
 *	Binayak Banerjee	(profiling enhancements)
 *	seismo!bpa!sjuvax!bbanerje
 *
 *	Michael Widenius:
 *	DBUG_DUMP	- To dump a pice of memory.
 *	PUSH_FLAG "O"	- To be used instead of "o" if we don't
 *			  want flushing (for slow systems)
 *	PUSH_FLAG "A"	- as 'O', but we will append to the out file instead
 *			  of creating a new one.
 *	Check of malloc on entry/exit (option "S")
 *
 *      Alexey Yurchenko:
 *      Renamed global symbols for use with galera project to avoid
 *      collisions with other software (notably MySQL)
 *
 * $Id$
 */

#ifndef _dbug_h
#define _dbug_h

#include <stdio.h>
#include <sys/types.h>

typedef unsigned int  uint;
typedef unsigned long ulong;

#define THREAD 1

#ifdef __cplusplus
extern "C"
{
#endif

    extern char  _gu_dig_vec[];
    extern FILE* _gu_db_fp_;

#define GU_DBUG_FILE _gu_db_fp_

#if defined(GU_DBUG_ON) && !defined(_lint)
    extern int   _gu_db_on_;
    extern int   _gu_no_db_;
    extern char* _gu_db_process_;
    extern int   _gu_db_keyword_(const char* keyword);
    extern void  _gu_db_setjmp_ (void);
    extern void  _gu_db_longjmp_(void);
    extern void  _gu_db_push_   (const char* control);
    extern void  _gu_db_pop_    (void);
    extern void  _gu_db_enter_  (const char* _func_,
                                 const char* _file_,
                                 uint _line_,
                                 const char** _sfunc_,
                                 const char** _sfile_,
                                 uint* _slevel_,
                                 char***);
    extern void  _gu_db_return_ (uint  _line_,
                                 const char** _sfunc_,
                                 const char** _sfile_,
                                 uint* _slevel_);
    extern void  _gu_db_pargs_  (uint _line_,
                                 const char* keyword);
    extern void  _gu_db_doprnt_ (const char* format,
                                 ...);
    extern void  _gu_db_dump_   (uint _line_,
                                 const char *keyword,
                                 const char *memory,
                                 uint length);
    extern void  _gu_db_lock_file  (void);
    extern void  _gu_db_unlock_file(void);


#define GU_DBUG_ENTER(a) \
        const char *_gu_db_func_, *_gu_db_file_; \
        uint _gu_db_level_; \
        char **_gu_db_framep_; \
        _gu_db_enter_ (a, __FILE__, __LINE__, &_gu_db_func_, &_gu_db_file_, \
                       &_gu_db_level_, &_gu_db_framep_)

#define GU_DBUG_LEAVE \
        (_gu_db_return_ (__LINE__, &_gu_db_func_, &_gu_db_file_, \
                         &_gu_db_level_))

#define GU_DBUG_RETURN(a1)  {GU_DBUG_LEAVE; return(a1);}
#define GU_DBUG_VOID_RETURN {GU_DBUG_LEAVE; return;    }

#define GU_DBUG_EXECUTE(keyword,a1) \
        {if (_gu_db_on_) {if (_gu_db_keyword_ (keyword)) { a1 }}}

#define GU_DBUG_PRINT(keyword,arglist) \
        {if (_gu_db_on_) {_gu_db_pargs_(__LINE__,keyword); \
         _gu_db_doprnt_ arglist;}}

#define GU_DBUG_PUSH(a1)        _gu_db_push_ (a1)
#define GU_DBUG_POP()           _gu_db_pop_  ()
#define GU_DBUG_PROCESS(a1)    (_gu_db_process_ = a1)
#define GU_DBUG_SETJMP(a1)     (_gu_db_setjmp_  (), setjmp  (a1))
#define GU_DBUG_LONGJMP(a1,a2) (_gu_db_longjmp_ (), longjmp (a1, a2))

#define GU_DBUG_DUMP(keyword,a1,a2)\
        {if (_gu_db_on_) {_gu_db_dump_(__LINE__,keyword,a1,a2);}}

#define GU_DBUG_IN_USE (_gu_db_fp_ && _gu_db_fp_ != stderr)
#define GU_DEBUGGER_OFF _no_gu_db_=1;_gu_db_on_=0;
#define GU_DEBUGGER_ON  _no_gu_db_=0
#define GU_DBUG_my_pthread_mutex_lock_FILE { _gu_db_lock_file(); }
#define GU_DBUG_my_pthread_mutex_unlock_FILE { _gu_db_unlock_file(); }
#define GU_DBUG_ASSERT(A) assert(A)

#else  /* No debugger */

#define GU_DBUG_ENTER(a1)
#define GU_DBUG_RETURN(a1) return(a1)
#define GU_DBUG_VOID_RETURN return
#define GU_DBUG_EXECUTE(keyword,a1) {}
#define GU_DBUG_PRINT(keyword,arglist) {}
#define GU_DBUG_PUSH(a1) {}
#define GU_DBUG_POP() {}
#define GU_DBUG_PROCESS(a1) {}
#define GU_DBUG_SETJMP setjmp
#define GU_DBUG_LONGJMP longjmp
#define GU_DBUG_DUMP(keyword,a1,a2) {}
#define GU_DBUG_IN_USE 0
#define GU_DEBUGGER_OFF
#define GU_DEBUGGER_ON
#define GU_DBUG_my_pthread_mutex_lock_FILE
#define GU_DBUG_my_pthread_mutex_unlock_FILE
#define GU_DBUG_ASSERT(A) {}
#endif
#ifdef __cplusplus
}
#endif
#endif
