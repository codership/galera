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
 *	PUSH_FLAG "O"	- To be used insted of "o" if we don't
 *			  want flushing (for slow systems)
 *	PUSH_FLAG "A"	- as 'O', but we will append to the out file instead
 *			  of creating a new one.
 *	Check of malloc on entry/exit (option "S")
 *
 *      Alexey Yurchenko:
 *      - Renamed global symbols for use with galera project to avoid
 *        collisions with other software (notably MySQL)
 *
 *      Teemu Ollakka:
 *      - Slight cleanups, removed some MySQL dependencies.
 *      - All global variables should now have _gu_db prefix.
 *      - Thread -> state mapping for multithreaded programs.
 *      - Changed initialization so that it is done on the first
 *        call to _gu_db_push().
 *
 * $Id$
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>

#ifndef GU_DBUG_ON
#define GU_DBUG_ON
#endif

#include "gu_dbug.h"

/* Make a new type: bool_t */
typedef enum
{
    FALSE = (0 != 0),
    TRUE = (!FALSE)
}
bool_t;

#define _VARARGS(X) X
#define FN_LIBCHAR 1024
#define FN_REFLEN  1024
#define NullS ""

#include <errno.h>
#if defined(MSDOS) || defined(__WIN__)
#include <process.h>
#endif

#ifdef  _GU_DBUG_CONDITION_
#define _GU_DBUG_START_CONDITION_ "d:t"
#else
#define _GU_DBUG_START_CONDITION_ ""
#endif

/*
 *   Manifest constants that should not require any changes.
 */

#define EOS             '\000'    /* End Of String marker */

/*
 *   Manifest constants which may be "tuned" if desired.
 */

#define PRINTBUF          1024    /* Print buffer size           */
#define INDENT               2    /* Indentation per trace level */
#define MAXDEPTH           200    /* Maximum trace depth default */

/*
 *   The following flags are used to determine which
 *   capabilities the user has enabled with the state
 *   push macro.
 */

#define TRACE_ON	000001	  /* Trace enabled                      */
#define DEBUG_ON	000002	  /* Debug enabled                      */
#define FILE_ON		000004	  /* File name print enabled            */
#define LINE_ON		000010	  /* Line number print enabled          */
#define DEPTH_ON	000020	  /* Function nest level print enabled  */
#define PROCESS_ON	000040	  /* Process name print enabled         */
#define NUMBER_ON	000100	  /* Number each line of output         */
#define PROFILE_ON	000200	  /* Print out profiling code           */
#define PID_ON		000400	  /* Identify each line with process id */
#define SANITY_CHECK_ON 001000	  /* Check my_malloc on GU_DBUG_ENTER   */
#define FLUSH_ON_WRITE	002000	  /* Flush on every write               */

#define TRACING    (_gu_db_stack -> flags & TRACE_ON)
#define DEBUGGING  (_gu_db_stack -> flags & DEBUG_ON)
#define PROFILING  (_gu_db_stack -> flags & PROFILE_ON)

#define STREQ(a,b) (strcmp(a,b) == 0)
#define min(a,b)   ((a) < (b) ? (a) : (b))
#define max(a,b)   ((a) > (b) ? (a) : (b))

/*
 *	Typedefs to make things more obvious.???
 */

#ifndef __WIN__
typedef int BOOLEAN;
#else
#define BOOLEAN BOOL
#endif

/*
 *	Make it easy to change storage classes if necessary.
 */

#define IMPORT   extern         /* Names defined externally           */
#define EXPORT                  /* Allocated here, available globally */
#define AUTO     auto           /* Names to be allocated on stack     */
#define REGISTER register       /* Names to be placed in registers    */

/*
 * The default file for profiling.  Could also add another flag
 * (G?) which allowed the user to specify this.
 *
 * If the automatic variables get allocated on the stack in
 * reverse order from their declarations, then define AUTOS_REVERSE.
 * This is used by the code that keeps track of stack usage.  For
 * forward allocation, the difference in the dbug frame pointers
 * represents stack used by the callee function.  For reverse allocation,
 * the difference represents stack used by the caller function.
 *
 */

#define PROF_FILE	"dbugmon.out"
#define PROF_EFMT	"E\t%ld\t%s\n"
#define PROF_SFMT	"S\t%lx\t%lx\t%s\n"
#define PROF_XFMT	"X\t%ld\t%s\n"

#ifdef M_I386		/* predefined by xenix 386 compiler */
#define AUTOS_REVERSE 1
#endif

/*
 *	Variables which are available externally but should only
 *	be accessed via the macro package facilities.
 */

FILE *_gu_db_fp_      = (FILE*) 0;       /* Output stream, default stderr    */
char *_gu_db_process_ = (char*) "dbug";  /* Pointer to process name; argv[0] */
FILE *_gu_db_pfp_     = (FILE*) 0;       /* Profile stream, 'dbugmon.out'    */
BOOLEAN _gu_db_on_    = FALSE;	         /* TRUE if debugging currently on   */
BOOLEAN _gu_db_pon_   = FALSE;	         /* TRUE if profile currently on     */
BOOLEAN _gu_no_db_    = TRUE;	         /* TRUE if no debugging at all      */

/*
 *	Externally supplied functions.
 */



IMPORT int _sanity(const char *file, uint line);

/*
 *	The user may specify a list of functions to trace or
 *	debug.	These lists are kept in a linear linked list,
 *	a very simple implementation.
 */

struct link
{
    char *str;	                /* Pointer to link's contents */
    struct link *next_link;     /* Pointer to the next link   */
};

/*
 *	Debugging states can be pushed or popped off of a
 *	stack which is implemented as a linked list.  Note
 *	that the head of the list is the current state and the
 *	stack is pushed by adding a new state to the head of the
 *	list or popped by removing the first link.
 */

struct state
{
    int   flags;                /* Current state flags             */
    int   maxdepth;             /* Current maximum trace depth     */
    uint  delay;                /* Delay after each output line    */
    int   sub_level;            /* Sub this from code_state->level */
    FILE* out_file;             /* Current output stream           */
    FILE* prof_file;            /* Current profiling stream        */
    char  name[FN_REFLEN];      /* Name of output file             */
    struct link*  functions;    /* List of functions               */
    struct link*  p_functions;  /* List of profiled functions      */
    struct link*  keywords;     /* List of debug keywords          */
    struct link*  processes;    /* List of process names           */
    struct state* next_state;   /* Next state in the list          */
};


/*
 *	Local variables not seen by user.
 */


static struct state* _gu_db_stack = 0;

typedef struct st_code_state
{
    int lineno;                 /* Current debugger output line number  */
    int level;		        /* Current function nesting level       */
    const char* func;	        /* Name of current user function        */
    const char* file;	        /* Name of current user file            */
    char** framep;	        /* Pointer to current frame             */
    int    jmplevel;	        /* Remember nesting level at setjmp ()  */
    const char* jmpfunc;        /* Remember current function for setjmp */
    const char* jmpfile;        /* Remember current file for setjmp     */

/*
 *	The following variables are used to hold the state information
 *	between the call to _gu_db_pargs_() and _gu_db_doprnt_(), during
 *	expansion of the GU_DBUG_PRINT macro.  This is the only macro
 *	that currently uses these variables.
 *
 *	These variables are currently used only by _gu_db_pargs_() and
 *	_gu_db_doprnt_().
 */

    uint u_line;                /* User source code line number    */
    const char* u_keyword;      /* Keyword for current macro       */
    int locked;                 /* If locked with _gu_db_lock_file */
}
CODE_STATE;

	/* Parse a debug command string */
static struct link *ListParse(char *ctlp);

	/* Make a fresh copy of a string */
static char *StrDup(const char *str);

	/* Open debug output stream */
static void GU_DBUGOpenFile(const char *name, int append);

#ifndef THREAD
	/* Open profile output stream */
static FILE *OpenProfile(const char *name);

	/* Profile if asked for it */
static BOOLEAN DoProfile(void);

	/* Return current user time (ms) */
static unsigned long Clock(void);
#endif

	/* Close debug output stream */
static void CloseFile(FILE * fp);
	/* Push current debug state */
static void PushState(void);
	/* Test for tracing enabled */
static BOOLEAN DoTrace(CODE_STATE * state);
	/* Test to see if file is writable */
#if !(!defined(HAVE_ACCESS) || defined(MSDOS))
static BOOLEAN Writable(char *pathname);
	/* Change file owner and group */
static void ChangeOwner(char *pathname);
	/* Allocate memory for runtime support */
#endif
static char *DbugMalloc(int size);
	/* Remove leading pathname components */
static char *BaseName(const char *pathname);
static void DoPrefix(uint line);
static void FreeList(struct link *linkp);
static void Indent(int indent);
static BOOLEAN InList(struct link *linkp, const char *cp);
static void dbug_flush(CODE_STATE *);
static void DbugExit(const char *why);
static int DelayArg(int value);
	/* Supplied in Sys V runtime environ */
	/* Break string into tokens */
static char *static_strtok(char *s1, char chr);

/*
 *	Miscellaneous printf format strings.
 */

#define ERR_MISSING_RETURN "%s: missing GU_DBUG_RETURN or GU_DBUG_VOID_RETURN macro in function \"%s\"\n"
#define ERR_OPEN "%s: can't open debug output stream \"%s\": "
#define ERR_CLOSE "%s: can't close debug file: "
#define ERR_ABORT "%s: debugger aborting because %s\n"
#define ERR_CHOWN "%s: can't change owner/group of \"%s\": "

/*
 *	Macros and defines for testing file accessibility under UNIX and MSDOS.
 */

#undef EXISTS
#if !defined(HAVE_ACCESS) || defined(MSDOS)
#define EXISTS(pathname) (FALSE)			   /* Assume no existance */
#define Writable(name) (TRUE)
#else
#define EXISTS(pathname)	 (access (pathname, F_OK) == 0)
#define WRITABLE(pathname)	 (access (pathname, W_OK) == 0)
#endif
#ifndef MSDOS
#define ChangeOwner(name)
#endif

/*
 *	Translate some calls among different systems.
 */

#if defined(unix) || defined(xenix) || defined(VMS) || defined(__NetBSD__)
# define Delay(A) sleep((uint) A)
#elif defined(AMIGA)
IMPORT int Delay();					   /* Pause for given number of ticks */
#else
static int Delay(int ticks);
#endif


/*
** Macros to allow dbugging with threads
*/

#ifdef THREAD


#include <pthread.h>

pthread_once_t _gu_db_once = PTHREAD_ONCE_INIT;
pthread_mutex_t _gu_db_mutex = PTHREAD_MUTEX_INITIALIZER;




struct state_map {
    pthread_t th;
    CODE_STATE *state;
    struct state_map *prev;
    struct state_map *next;
};
#define _GU_DB_STATE_MAP_BUCKETS (1 << 7)
static struct state_map *_gu_db_state_map[_GU_DB_STATE_MAP_BUCKETS];

/*
 * This hash is probably good enough. Golden ratio 2654435761U from 
 * http://www.concentric.net/~Ttwang/tech/inthash.htm
 *
 * UPDATE: it is good enough for input with significant variation in
 *         32 lower bits.
 */
static inline unsigned long pt_hash(const pthread_t th)
{
    unsigned long k = (unsigned long)th;
    uint64_t ret = 2654435761U * k;
    // since we're returning a masked hash key, all considerations
    // for "reversibility" can be dropped. Instead we can help
    // higher input bits influence lower output bits. XOR rules.
    return (ret ^ (ret >> 32)) & (_GU_DB_STATE_MAP_BUCKETS - 1);
}

static CODE_STATE *state_map_find(const pthread_t th)
{
    unsigned int key = pt_hash(th);
    struct state_map *sm = _gu_db_state_map[key];
    while (sm && sm->th != th)
	sm = sm->next;
    return sm ? sm->state : NULL;
}

void state_map_insert(const pthread_t th, CODE_STATE *state)
{

    unsigned int key;
    struct state_map *sm;
    
    assert(state_map_find(th) == NULL);
    
    key = pt_hash(th);


    sm = malloc(sizeof(struct state_map));
    sm->state = state;
    sm->th = th;

    pthread_mutex_lock(&_gu_db_mutex);
    
    sm->prev = NULL;
    sm->next = _gu_db_state_map[key];
    if (sm->next)
	sm->next->prev = sm;
    _gu_db_state_map[key] = sm;
    
    pthread_mutex_unlock(&_gu_db_mutex);
}

void state_map_erase(const pthread_t th)
{
    unsigned int key;
    struct state_map *sm;

    key = pt_hash(th);
    sm = _gu_db_state_map[key];
    while (sm && sm->th != th)
	sm = sm->next;
    assert(sm);
    pthread_mutex_lock(&_gu_db_mutex);
    if (sm->prev) {
	sm->prev->next = sm->next;
    } else {
	assert(_gu_db_state_map[key] == sm);
	_gu_db_state_map[key] = sm->next;
    }
    if (sm->next)
	sm->next->prev = sm->prev;
    pthread_mutex_unlock(&_gu_db_mutex);
    free(sm);
}


static CODE_STATE *
code_state(void)
{
    CODE_STATE *state = 0;
    if ((state = state_map_find(pthread_self())) == NULL) {
	state = malloc(sizeof(CODE_STATE));
	memset(state, 0, sizeof(CODE_STATE));
	state->func = "?func";
	state->file = "?file";
	state->u_keyword = "?";
	state_map_insert(pthread_self(), state);
    }
    return state;
}

static void code_state_cleanup(CODE_STATE *state)
{
    if (state->level == 0) {
	state_map_erase(pthread_self());
	free(state);
    }
}

static void _gu_db_init()
{
    if (!_gu_db_fp_)
	_gu_db_fp_ = stderr;	 /* Output stream, default stderr */    
    memset(_gu_db_state_map, 0, sizeof(_gu_db_state_map));
}

#else /* !THREAD */
#define _gu_db_init()
#define code_state() (&static_code_state)
#define code_state_cleanup(A) do {} while (0)
#define pthread_mutex_lock(A) {}
#define pthread_mutex_unlock(A) {}
static CODE_STATE static_code_state = { 0, 0, "?func", "?file", NULL, 0, NULL,
    NULL, 0, "?", 0
};
#endif


/*
 *  FUNCTION
 *
 *	_gu_db_push_	push current debugger state and set up new one
 *
 *  SYNOPSIS
 *
 *	VOID _gu_db_push_ (control)
 *	char *control;
 *
 *  DESCRIPTION
 *
 *	Given pointer to a debug control string in "control", pushes
 *	the current debug state, parses the control string, and sets
 *	up a new debug state.
 *
 *	The only attribute of the new state inherited from the previous
 *	state is the current function nesting level.  This can be
 *	overridden by using the "r" flag in the control string.
 *
 *	The debug control string is a sequence of colon separated fields
 *	as follows:
 *
 *		<field_1>:<field_2>:...:<field_N>
 *
 *	Each field consists of a mandatory flag character followed by
 *	an optional "," and comma separated list of modifiers:
 *
 *		flag[,modifier,modifier,...,modifier]
 *
 *	The currently recognized flag characters are:
 *
 *		d	Enable output from GU_DBUG_<N> macros for
 *			for the current state.	May be followed
 *			by a list of keywords which selects output
 *			only for the GU_DBUG macros with that keyword.
 *			A null list of keywords implies output for
 *			all macros.
 *
 *		D	Delay after each debugger output line.
 *			The argument is the number of tenths of seconds
 *			to delay, subject to machine capabilities.
 *			I.E.  -#D,20 is delay two seconds.
 *
 *		f	Limit debugging and/or tracing, and profiling to the
 *			list of named functions.  Note that a null list will
 *			disable all functions.	The appropriate "d" or "t"
 *			flags must still be given, this flag only limits their
 *			actions if they are enabled.
 *
 *		F	Identify the source file name for each
 *			line of debug or trace output.
 *
 *		i	Identify the process with the pid for each line of
 *			debug or trace output.
 *
 *		g	Enable profiling.  Create a file called 'dbugmon.out'
 *			containing information that can be used to profile
 *			the program.  May be followed by a list of keywords
 *			that select profiling only for the functions in that
 *			list.  A null list implies that all functions are
 *			considered.
 *
 *		L	Identify the source file line number for
 *			each line of debug or trace output.
 *
 *		n	Print the current function nesting depth for
 *			each line of debug or trace output.
 *
 *		N	Number each line of dbug output.
 *
 *		o	Redirect the debugger output stream to the
 *			specified file.  The default output is stderr.
 *
 *		O	As O but the file is really flushed between each
 *			write. When neaded the file is closed and reopened
 *			between each write.
 *
 *		p	Limit debugger actions to specified processes.
 *			A process must be identified with the
 *			GU_DBUG_PROCESS macro and match one in the list
 *			for debugger actions to occur.
 *
 *		P	Print the current process name for each
 *			line of debug or trace output.
 *
 *		r	When pushing a new state, do not inherit
 *			the previous state's function nesting level.
 *			Useful when the output is to start at the
 *			left margin.
 *
 *		S	Do function _sanity(_file_,_line_) at each
 *			debugged function until _sanity() returns
 *			something that differs from 0.
 *			(Moustly used with my_malloc)
 *
 *		t	Enable function call/exit trace lines.
 *			May be followed by a list (containing only
 *			one modifier) giving a numeric maximum
 *			trace level, beyond which no output will
 *			occur for either debugging or tracing
 *			macros.  The default is a compile time
 *			option.
 *
 *	Some examples of debug control strings which might appear
 *	on a shell command line (the "-#" is typically used to
 *	introduce a control string to an application program) are:
 *
 *		-#d:t
 *		-#d:f,main,subr1:F:L:t,20
 *		-#d,input,output,files:n
 *
 *	For convenience, any leading "-#" is stripped off.
 *
 */

void
_gu_db_push_(const char *control)
{
    register char *scan;
    register struct link *temp;
    CODE_STATE *state;
    char *new_str;

    pthread_once(&_gu_db_once, &_gu_db_init);

    if (control && *control == '-') {
	if (*++control == '#')
	    control++;
    }
    if (*control)
	_gu_no_db_ = FALSE;		    /* We are using dbug after all */
    else
	return;

    new_str = StrDup(control);
    PushState();
    state = code_state();

    scan = static_strtok(new_str, ':');
    for (; scan != NULL; scan = static_strtok((char *) NULL, ':')) {
	switch (*scan++) {
	case 'd':
	    _gu_db_on_ = TRUE;
	    _gu_db_stack->flags |= DEBUG_ON;
	    if (*scan++ == ',') {
		_gu_db_stack->keywords = ListParse(scan);
	    }
	    break;
	case 'D':
	    _gu_db_stack->delay = 0;
	    if (*scan++ == ',') {
		temp = ListParse(scan);
		_gu_db_stack->delay = DelayArg(atoi(temp->str));
		FreeList(temp);
	    }
	    break;
	case 'f':
	    if (*scan++ == ',') {
		_gu_db_stack->functions = ListParse(scan);
	    }
	    break;
	case 'F':
	    _gu_db_stack->flags |= FILE_ON;
	    break;
	case 'i':
	    _gu_db_stack->flags |= PID_ON;
	    break;
#ifndef THREAD
	case 'g':
	    _gu_db_pon_ = TRUE;
	    if (OpenProfile(PROF_FILE)) {
		_gu_db_stack->flags |= PROFILE_ON;
		if (*scan++ == ',')
		    _gu_db_stack->p_functions = ListParse(scan);
	    }
	    break;
#endif
	case 'L':
	    _gu_db_stack->flags |= LINE_ON;
	    break;
	case 'n':
	    _gu_db_stack->flags |= DEPTH_ON;
	    break;
	case 'N':
	    _gu_db_stack->flags |= NUMBER_ON;
	    break;
	case 'A':
	case 'O':
	    _gu_db_stack->flags |= FLUSH_ON_WRITE;
            // fall through
	case 'a':
	case 'o':
	    if (*scan++ == ',') {
		temp = ListParse(scan);
		GU_DBUGOpenFile(temp->str, (int) (scan[-2] == 'A'
			|| scan[-2] == 'a'));
		FreeList(temp);
	    } else {
		GU_DBUGOpenFile("-", 0);
	    }
	    break;
	case 'p':
	    if (*scan++ == ',') {
		_gu_db_stack->processes = ListParse(scan);
	    }
	    break;
	case 'P':
	    _gu_db_stack->flags |= PROCESS_ON;
	    break;
	case 'r':
	    _gu_db_stack->sub_level = state->level;
	    break;
	case 't':
	    _gu_db_stack->flags |= TRACE_ON;
	    if (*scan++ == ',') {
		temp = ListParse(scan);
		_gu_db_stack->maxdepth = atoi(temp->str);
		FreeList(temp);
	    }
	    break;
	case 'S':
	    _gu_db_stack->flags |= SANITY_CHECK_ON;
	    break;
	}
    }
    free(new_str);
}


/*
 *  FUNCTION
 *
 *	_gu_db_pop_    pop the debug stack
 *
 *  DESCRIPTION
 *
 *	Pops the debug stack, returning the debug state to its
 *	condition prior to the most recent _gu_db_push_ invocation.
 *	Note that the pop will fail if it would remove the last
 *	valid state from the stack.  This prevents user errors
 *	in the push/pop sequence from screwing up the debugger.
 *	Maybe there should be some kind of warning printed if the
 *	user tries to pop too many states.
 *
 */

void
_gu_db_pop_()
{
    register struct state *discard;
    discard = _gu_db_stack;
    if (discard != NULL && discard->next_state != NULL) {
	_gu_db_stack = discard->next_state;
	_gu_db_fp_ = _gu_db_stack->out_file;
	_gu_db_pfp_ = _gu_db_stack->prof_file;
	if (discard->keywords != NULL) {
	    FreeList(discard->keywords);
	}
	if (discard->functions != NULL) {
	    FreeList(discard->functions);
	}
	if (discard->processes != NULL) {
	    FreeList(discard->processes);
	}
	if (discard->p_functions != NULL) {
	    FreeList(discard->p_functions);
	}
	CloseFile(discard->out_file);
	if (discard->prof_file)
	    CloseFile(discard->prof_file);
	free((char *) discard);
	if (!(_gu_db_stack->flags & DEBUG_ON))
	    _gu_db_on_ = 0;
    } else {
        if (_gu_db_stack)
            _gu_db_stack->flags &= ~DEBUG_ON;
	_gu_db_on_ = 0;
    }
}


/*
 *  FUNCTION
 *
 *	_gu_db_enter_    process entry point to user function
 *
 *  SYNOPSIS
 *
 *	VOID _gu_db_enter_ (_func_, _file_, _line_,
 *			 _sfunc_, _sfile_, _slevel_, _sframep_)
 *	char *_func_;		points to current function name
 *	char *_file_;		points to current file name
 *	int _line_;		called from source line number
 *	char **_sfunc_;		save previous _func_
 *	char **_sfile_;		save previous _file_
 *	int *_slevel_;		save previous nesting level
 *	char ***_sframep_;	save previous frame pointer
 *
 *  DESCRIPTION
 *
 *	Called at the beginning of each user function to tell
 *	the debugger that a new function has been entered.
 *	Note that the pointers to the previous user function
 *	name and previous user file name are stored on the
 *	caller's stack (this is why the ENTER macro must be
 *	the first "executable" code in a function, since it
 *	allocates these storage locations).  The previous nesting
 *	level is also stored on the callers stack for internal
 *	self consistency checks.
 *
 *	Also prints a trace line if tracing is enabled and
 *	increments the current function nesting depth.
 *
 *	Note that this mechanism allows the debugger to know
 *	what the current user function is at all times, without
 *	maintaining an internal stack for the function names.
 *
 */

void
_gu_db_enter_(const char *_func_,
    const char *_file_,
    uint _line_,
    const char **_sfunc_,
    const char **_sfile_,
    uint * _slevel_, char ***_sframep_ __attribute__ ((unused)))
{
    register CODE_STATE *state;

    if (!_gu_no_db_) {
	int save_errno = errno;
	state = code_state();

	*_sfunc_ = state->func;
	*_sfile_ = state->file;
	state->func = (char *) _func_;
	state->file = (char *) _file_;			   /* BaseName takes time !! */
	*_slevel_ = ++state->level;
#ifndef THREAD
	*_sframep_ = state->framep;
	state->framep = (char **) _sframep_;
	if (DoProfile()) {
	    long stackused;
	    if (*state->framep == NULL) {
		stackused = 0;
	    } else {
		stackused =
		    ((long) (*state->framep)) - ((long) (state->framep));
		stackused = stackused > 0 ? stackused : -stackused;
	    }
	    (void) fprintf(_gu_db_pfp_, PROF_EFMT, Clock(), state->func);
#ifdef AUTOS_REVERSE
	    (void) fprintf(_gu_db_pfp_, PROF_SFMT, state->framep, stackused,
		*_sfunc_);
#else
	    (void) fprintf(_gu_db_pfp_, PROF_SFMT, (ulong) state->framep,
		stackused, state->func);
#endif
	    (void) fflush(_gu_db_pfp_);
	}
#endif
	if (DoTrace(state)) {
	    if (!state->locked)
		pthread_mutex_lock(&_gu_db_mutex);
	    DoPrefix(_line_);
	    Indent(state->level);
	    (void) fprintf(_gu_db_fp_, ">%s\n", state->func);
	    dbug_flush(state);				   /* This does a unlock */
	}
#ifdef SAFEMALLOC
	if (_gu_db_stack->flags & SANITY_CHECK_ON)
	    if (_sanity(_file_, _line_))		   /* Check of my_malloc */
		_gu_db_stack->flags &= ~SANITY_CHECK_ON;
#endif
	errno = save_errno;
    }
}

/*
 *  FUNCTION
 *
 *	_gu_db_return_    process exit from user function
 *
 *  SYNOPSIS
 *
 *	VOID _gu_db_return_ (_line_, _sfunc_, _sfile_, _slevel_)
 *	int _line_;		current source line number
 *	char **_sfunc_;		where previous _func_ is to be retrieved
 *	char **_sfile_;		where previous _file_ is to be retrieved
 *	int *_slevel_;		where previous level was stashed
 *
 *  DESCRIPTION
 *
 *	Called just before user function executes an explicit or implicit
 *	return.  Prints a trace line if trace is enabled, decrements
 *	the current nesting level, and restores the current function and
 *	file names from the defunct function's stack.
 *
 */

void
_gu_db_return_(uint _line_,
    const char **_sfunc_, const char **_sfile_, uint * _slevel_)
{
    CODE_STATE *state;

    if (!_gu_no_db_) {
	int save_errno = errno;
	if (!(state = code_state()))
	    return;					   /* Only happens at end of program */
	if (_gu_db_stack->flags & (TRACE_ON | DEBUG_ON | PROFILE_ON)) {
	    if (!state->locked)
		pthread_mutex_lock(&_gu_db_mutex);
	    if (state->level != (int) *_slevel_)
		(void) fprintf(_gu_db_fp_, ERR_MISSING_RETURN, _gu_db_process_,
		    state->func);
	    else {
#ifdef SAFEMALLOC
		if (_gu_db_stack->flags & SANITY_CHECK_ON)
		    if (_sanity(*_sfile_, _line_))
			_gu_db_stack->flags &= ~SANITY_CHECK_ON;
#endif
#ifndef THREAD
		if (DoProfile())
		    (void) fprintf(_gu_db_pfp_, PROF_XFMT, Clock(), state->func);
#endif
		if (DoTrace(state)) {
		    DoPrefix(_line_);
		    Indent(state->level);
		    (void) fprintf(_gu_db_fp_, "<%s\n", state->func);
		}
	    }
	    dbug_flush(state);
	}
	state->level = *_slevel_ - 1;
	state->func = *_sfunc_;
	state->file = *_sfile_;
#ifndef THREAD
	if (state->framep != NULL)
	    state->framep = (char **) *state->framep;
#endif
	errno = save_errno;
	code_state_cleanup(state);
    }
}


/*
 *  FUNCTION
 *
 *	_gu_db_pargs_    log arguments for subsequent use by _gu_db_doprnt_()
 *
 *  SYNOPSIS
 *
 *	VOID _gu_db_pargs_ (_line_, keyword)
 *	int _line_;
 *	char *keyword;
 *
 *  DESCRIPTION
 *
 *	The new universal printing macro GU_DBUG_PRINT, which replaces
 *	all forms of the GU_DBUG_N macros, needs two calls to runtime
 *	support routines.  The first, this function, remembers arguments
 *	that are used by the subsequent call to _gu_db_doprnt_().
 *
 */

void
_gu_db_pargs_(uint _line_, const char *keyword)
{
    CODE_STATE *state = code_state();
    state->u_line = _line_;
    state->u_keyword = (char *) keyword;
}


/*
 *  FUNCTION
 *
 *	_gu_db_doprnt_    handle print of debug lines
 *
 *  SYNOPSIS
 *
 *	VOID _gu_db_doprnt_ (format, va_alist)
 *	char *format;
 *	va_dcl;
 *
 *  DESCRIPTION
 *
 *	When invoked via one of the GU_DBUG macros, tests the current keyword
 *	set by calling _gu_db_pargs_() to see if that macro has been selected
 *	for processing via the debugger control string, and if so, handles
 *	printing of the arguments via the format string.  The line number
 *	of the GU_DBUG macro in the source is found in u_line.
 *
 *	Note that the format string SHOULD NOT include a terminating
 *	newline, this is supplied automatically.
 *
 */

#include <stdarg.h>

void
_gu_db_doprnt_(const char *format, ...)
{
    va_list args;
    CODE_STATE *state;
    state = code_state();

    va_start(args, format);

    if (_gu_db_keyword_(state->u_keyword)) {
	int save_errno = errno;
	if (!state->locked)
	    pthread_mutex_lock(&_gu_db_mutex);
	DoPrefix(state->u_line);
	if (TRACING) {
	    Indent(state->level + 1);
	} else {
	    (void) fprintf(_gu_db_fp_, "%s: ", state->func);
	}
	(void) fprintf(_gu_db_fp_, "%s: ", state->u_keyword);
	(void) vfprintf(_gu_db_fp_, format, args);
	va_end(args);
	(void) fputc('\n', _gu_db_fp_);
	dbug_flush(state);
	errno = save_errno;

    }
    va_end(args);

    code_state_cleanup(state);
}


/*
 *  FUNCTION
 *
 *	      _gu_db_dump_    dump a string until '\0' is found
 *
 *  SYNOPSIS
 *
 *	      void _gu_db_dump_ (_line_,keyword,memory,length)
 *	      int _line_;		current source line number
 *	      char *keyword;
 *	      char *memory;		Memory to print
 *	      int length;		Bytes to print
 *
 *  DESCRIPTION
 *  Dump N characters in a binary array.
 *  Is used to examine corrputed memory or arrays.
 */

void
_gu_db_dump_(uint _line_, const char *keyword, const char *memory, uint length)
{
    int pos;
    char dbuff[90];
    CODE_STATE *state;
    state = code_state();

    if (_gu_db_keyword_((char *) keyword)) {
	if (!state->locked)
	    pthread_mutex_lock(&_gu_db_mutex);
	DoPrefix(_line_);
	if (TRACING) {
	    Indent(state->level + 1);
	    pos = min(max(state->level - _gu_db_stack->sub_level, 0) * INDENT, 80);
	} else {
	    fprintf(_gu_db_fp_, "%s: ", state->func);
	}
	sprintf(dbuff, "%s: Memory: %lx  Bytes: (%d)\n",
	    keyword, (ulong) memory, length);
	(void) fputs(dbuff, _gu_db_fp_);

	pos = 0;
	while (length-- > 0) {
	    uint tmp = *((unsigned char *) memory++);
	    if ((pos += 3) >= 80) {
		fputc('\n', _gu_db_fp_);
		pos = 3;
	    }
	    fputc(_gu_dig_vec[((tmp >> 4) & 15)], _gu_db_fp_);
	    fputc(_gu_dig_vec[tmp & 15], _gu_db_fp_);
	    fputc(' ', _gu_db_fp_);
	}
	(void) fputc('\n', _gu_db_fp_);
	dbug_flush(state);
    }
    code_state_cleanup(state);
}

/*
 *  FUNCTION
 *
 *	ListParse    parse list of modifiers in debug control string
 *
 *  SYNOPSIS
 *
 *	static struct link *ListParse (ctlp)
 *	char *ctlp;
 *
 *  DESCRIPTION
 *
 *	Given pointer to a comma separated list of strings in "cltp",
 *	parses the list, building a list and returning a pointer to it.
 *	The original comma separated list is destroyed in the process of
 *	building the linked list, thus it had better be a duplicate
 *	if it is important.
 *
 *	Note that since each link is added at the head of the list,
 *	the final list will be in "reverse order", which is not
 *	significant for our usage here.
 *
 */

static struct link *
ListParse(char *ctlp)
{
    REGISTER char *start;
    REGISTER struct link *new_malloc;
    REGISTER struct link *head;

    head = NULL;
    while (*ctlp != EOS) {
	start = ctlp;
	while (*ctlp != EOS && *ctlp != ',') {
	    ctlp++;
	}
	if (*ctlp == ',') {
	    *ctlp++ = EOS;
	}
	new_malloc = (struct link *) DbugMalloc(sizeof(struct link));
	new_malloc->str = StrDup(start);
	new_malloc->next_link = head;
	head = new_malloc;
    }
    return (head);
}

/*
 *  FUNCTION
 *
 *	InList	  test a given string for member of a given list
 *
 *  SYNOPSIS
 *
 *	static BOOLEAN InList (linkp, cp)
 *	struct link *linkp;
 *	char *cp;
 *
 *  DESCRIPTION
 *
 *	Tests the string pointed to by "cp" to determine if it is in
 *	the list pointed to by "linkp".  Linkp points to the first
 *	link in the list.  If linkp is NULL then the string is treated
 *	as if it is in the list (I.E all strings are in the null list).
 *	This may seem rather strange at first but leads to the desired
 *	operation if no list is given.	The net effect is that all
 *	strings will be accepted when there is no list, and when there
 *	is a list, only those strings in the list will be accepted.
 *
 */

static BOOLEAN
InList(struct link *linkp, const char *cp)
{
    REGISTER struct link *scan;
    REGISTER BOOLEAN result;

    if (linkp == NULL) {
	result = TRUE;
    } else {
	result = FALSE;
	for (scan = linkp; scan != NULL; scan = scan->next_link) {
	    if (STREQ(scan->str, cp)) {
		result = TRUE;
		break;
	    }
	}
    }
    return (result);
}


/*
 *  FUNCTION
 *
 *	PushState    push current state onto stack and set up new one
 *
 *  SYNOPSIS
 *
 *	static VOID PushState ()
 *
 *  DESCRIPTION
 *
 *	Pushes the current state on the state stack, and inits
 *	a new state.  The only parameter inherited from the previous
 *	state is the function nesting level.  This action can be
 *	inhibited if desired, via the "r" flag.
 *
 *	The state stack is a linked list of states, with the new
 *	state added at the head.  This allows the stack to grow
 *	to the limits of memory if necessary.
 *
 */

static void
PushState()
{
    REGISTER struct state *new_malloc;

    new_malloc = (struct state *) DbugMalloc(sizeof(struct state));
    new_malloc->flags = 0;
    new_malloc->delay = 0;
    new_malloc->maxdepth = MAXDEPTH;
    new_malloc->sub_level = 0;
    new_malloc->out_file = stderr;
    new_malloc->prof_file = (FILE *) 0;
    new_malloc->functions = NULL;
    new_malloc->p_functions = NULL;
    new_malloc->keywords = NULL;
    new_malloc->processes = NULL;
    new_malloc->next_state = _gu_db_stack;
    _gu_db_stack = new_malloc;
}


/*
 *  FUNCTION
 *
 *	DoTrace    check to see if tracing is current enabled
 *
 *  SYNOPSIS
 *
 *	static BOOLEAN DoTrace (stack)
 *
 *  DESCRIPTION
 *
 *	Checks to see if tracing is enabled based on whether the
 *	user has specified tracing, the maximum trace depth has
 *	not yet been reached, the current function is selected,
 *	and the current process is selected.  Returns TRUE if
 *	tracing is enabled, FALSE otherwise.
 *
 */

static BOOLEAN
DoTrace(CODE_STATE * state)
{
    register BOOLEAN trace = FALSE;

    if (TRACING &&
	state->level <= _gu_db_stack->maxdepth &&
	InList(_gu_db_stack->functions, state->func) &&
	InList(_gu_db_stack->processes, _gu_db_process_))
	trace = TRUE;
    return (trace);
}


/*
 *  FUNCTION
 *
 *	DoProfile    check to see if profiling is current enabled
 *
 *  SYNOPSIS
 *
 *	static BOOLEAN DoProfile ()
 *
 *  DESCRIPTION
 *
 *	Checks to see if profiling is enabled based on whether the
 *	user has specified profiling, the maximum trace depth has
 *	not yet been reached, the current function is selected,
 *	and the current process is selected.  Returns TRUE if
 *	profiling is enabled, FALSE otherwise.
 *
 */

#ifndef THREAD
static BOOLEAN
DoProfile()
{
    REGISTER BOOLEAN profile;
    CODE_STATE *state;
    state = code_state();

    profile = FALSE;
    if (PROFILING &&
	state->level <= _gu_db_stack->maxdepth &&
	InList(_gu_db_stack->p_functions, state->func) &&
	InList(_gu_db_stack->processes, _gu_db_process_))
	profile = TRUE;
    return (profile);
}
#endif


/*
 *  FUNCTION
 *
 *	_gu_db_keyword_	test keyword for member of keyword list
 *
 *  SYNOPSIS
 *
 *	BOOLEAN _gu_db_keyword_ (keyword)
 *	char *keyword;
 *
 *  DESCRIPTION
 *
 *	Test a keyword to determine if it is in the currently active
 *	keyword list.  As with the function list, a keyword is accepted
 *	if the list is null, otherwise it must match one of the list
 *	members.  When debugging is not on, no keywords are accepted.
 *	After the maximum trace level is exceeded, no keywords are
 *	accepted (this behavior subject to change).  Additionally,
 *	the current function and process must be accepted based on
 *	their respective lists.
 *
 *	Returns TRUE if keyword accepted, FALSE otherwise.
 *
 */

BOOLEAN
_gu_db_keyword_(const char *keyword)
{
    REGISTER BOOLEAN result;
    CODE_STATE *state;

    state = code_state();
    result = FALSE;
    if (DEBUGGING &&
	state->level <= _gu_db_stack->maxdepth &&
	InList(_gu_db_stack->functions, state->func) &&
	InList(_gu_db_stack->keywords, keyword) &&
	InList(_gu_db_stack->processes, _gu_db_process_))
	result = TRUE;
    return (result);
}

/*
 *  FUNCTION
 *
 *	Indent	  indent a line to the given indentation level
 *
 *  SYNOPSIS
 *
 *	static VOID Indent (indent)
 *	int indent;
 *
 *  DESCRIPTION
 *
 *	Indent a line to the given level.  Note that this is
 *	a simple minded but portable implementation.
 *	There are better ways.
 *
 *	Also, the indent must be scaled by the compile time option
 *	of character positions per nesting level.
 *
 */

static void
Indent(int indent)
{
    REGISTER int count;

    indent = max(indent - 1 - _gu_db_stack->sub_level, 0) * INDENT;
    for (count = 0; count < indent; count++) {
	if ((count % INDENT) == 0)
	    fputc('|', _gu_db_fp_);
	else
	    fputc(' ', _gu_db_fp_);
    }
}


/*
 *  FUNCTION
 *
 *	FreeList    free all memory associated with a linked list
 *
 *  SYNOPSIS
 *
 *	static VOID FreeList (linkp)
 *	struct link *linkp;
 *
 *  DESCRIPTION
 *
 *	Given pointer to the head of a linked list, frees all
 *	memory held by the list and the members of the list.
 *
 */

static void
FreeList(struct link *linkp)
{
    REGISTER struct link *old;

    while (linkp != NULL) {
	old = linkp;
	linkp = linkp->next_link;
	if (old->str != NULL) {
	    free(old->str);
	}
	free((char *) old);
    }
}


/*
 *  FUNCTION
 *
 *	StrDup	 make a duplicate of a string in new memory
 *
 *  SYNOPSIS
 *
 *	static char *StrDup (my_string)
 *	char *string;
 *
 *  DESCRIPTION
 *
 *	Given pointer to a string, allocates sufficient memory to make
 *	a duplicate copy, and copies the string to the newly allocated
 *	memory.  Failure to allocated sufficient memory is immediately
 *	fatal.
 *
 */


static char *
StrDup(const char *str)
{
    register char *new_malloc;
    new_malloc = DbugMalloc((int) strlen(str) + 1);
    (void) strcpy(new_malloc, str);
    return (new_malloc);
}


/*
 *  FUNCTION
 *
 *	DoPrefix    print debugger line prefix prior to indentation
 *
 *  SYNOPSIS
 *
 *	static VOID DoPrefix (_line_)
 *	int _line_;
 *
 *  DESCRIPTION
 *
 *	Print prefix common to all debugger output lines, prior to
 *	doing indentation if necessary.  Print such information as
 *	current process name, current source file name and line number,
 *	and current function nesting depth.
 *
 */

static void
DoPrefix(uint _line_)
{
    CODE_STATE *state;
    state = code_state();

    state->lineno++;
    if (_gu_db_stack->flags & PID_ON) {
#ifdef THREAD
        (void) fprintf(_gu_db_fp_, "%5d:(thread %lu):", (int)getpid(), (unsigned long)pthread_self());
#else
	(void) fprintf(_gu_db_fp_, "%5d: ", (int) getpid());
#endif /* THREAD */
    }
    if (_gu_db_stack->flags & NUMBER_ON) {
	(void) fprintf(_gu_db_fp_, "%5d: ", state->lineno);
    }
    if (_gu_db_stack->flags & PROCESS_ON) {
	(void) fprintf(_gu_db_fp_, "%s: ", _gu_db_process_);
    }
    if (_gu_db_stack->flags & FILE_ON) {
	(void) fprintf(_gu_db_fp_, "%14s: ", BaseName(state->file));
    }
    if (_gu_db_stack->flags & LINE_ON) {
	(void) fprintf(_gu_db_fp_, "%5d: ", _line_);
    }
    if (_gu_db_stack->flags & DEPTH_ON) {
	(void) fprintf(_gu_db_fp_, "%4d: ", state->level);
    }
}


/*
 *  FUNCTION
 *
 *	GU_DBUGOpenFile	open new output stream for debugger output
 *
 *  SYNOPSIS
 *
 *	static VOID GU_DBUGOpenFile (name)
 *	char *name;
 *
 *  DESCRIPTION
 *
 *	Given name of a new file (or "-" for stdout) opens the file
 *	and sets the output stream to the new file.
 *
 */

static void
GU_DBUGOpenFile(const char *name, int append)
{
    REGISTER FILE *fp;
    REGISTER BOOLEAN newfile;

    if (name != NULL) {
	strcpy(_gu_db_stack->name, name);
	if (strlen(name) == 1 && name[0] == '-') {
	    _gu_db_fp_ = stdout;
	    _gu_db_stack->out_file = _gu_db_fp_;
	    _gu_db_stack->flags |= FLUSH_ON_WRITE;
	} else {
	    if (!Writable((char *) name)) {
		(void) fprintf(stderr, ERR_OPEN, _gu_db_process_, name);
		perror("");
		fflush(stderr);
	    } else {
		newfile = !EXISTS(name);
		if (!(fp = fopen(name, append ? "a+" : "w"))) {
		    (void) fprintf(stderr, ERR_OPEN, _gu_db_process_, name);
		    perror("");
		    fflush(stderr);
		} else {
		    _gu_db_fp_ = fp;
		    _gu_db_stack->out_file = fp;
		    if (newfile) {
			ChangeOwner(name);
		    }
		}
	    }
	}
    }
}


/*
 *  FUNCTION
 *
 *	OpenProfile    open new output stream for profiler output
 *
 *  SYNOPSIS
 *
 *	static FILE *OpenProfile (name)
 *	char *name;
 *
 *  DESCRIPTION
 *
 *	Given name of a new file, opens the file
 *	and sets the profiler output stream to the new file.
 *
 *	It is currently unclear whether the prefered behavior is
 *	to truncate any existing file, or simply append to it.
 *	The latter behavior would be desirable for collecting
 *	accumulated runtime history over a number of separate
 *	runs.  It might take some changes to the analyzer program
 *	though, and the notes that Binayak sent with the profiling
 *	diffs indicated that append was the normal mode, but this
 *	does not appear to agree with the actual code. I haven't
 *	investigated at this time [fnf; 24-Jul-87].
 */

#ifndef THREAD
static FILE *
OpenProfile(const char *name)
{
    REGISTER FILE *fp;
    REGISTER BOOLEAN newfile;

    fp = 0;
    if (!Writable(name)) {
	(void) fprintf(_gu_db_fp_, ERR_OPEN, _gu_db_process_, name);
	perror("");
	dbug_flush(0);
	(void) Delay(_gu_db_stack->delay);
    } else {
	newfile = !EXISTS(name);
	if (!(fp = fopen(name, "w"))) {
	    (void) fprintf(_gu_db_fp_, ERR_OPEN, _gu_db_process_, name);
	    perror("");
	    dbug_flush(0);
	} else {
	    _gu_db_pfp_ = fp;
	    _gu_db_stack->prof_file = fp;
	    if (newfile) {
		ChangeOwner(name);
	    }
	}
    }
    return fp;
}
#endif

/*
 *  FUNCTION
 *
 *	CloseFile    close the debug output stream
 *
 *  SYNOPSIS
 *
 *	static VOID CloseFile (fp)
 *	FILE *fp;
 *
 *  DESCRIPTION
 *
 *	Closes the debug output stream unless it is standard output
 *	or standard error.
 *
 */

static void
CloseFile(FILE * fp)
{
    if (fp != stderr && fp != stdout) {
	if (fclose(fp) == EOF) {
	    pthread_mutex_lock(&_gu_db_mutex);
	    (void) fprintf(_gu_db_fp_, ERR_CLOSE, _gu_db_process_);
	    perror("");
	    dbug_flush(0);
	}
    }
}


/*
 *  FUNCTION
 *
 *	DbugExit    print error message and exit
 *
 *  SYNOPSIS
 *
 *	static VOID DbugExit (why)
 *	char *why;
 *
 *  DESCRIPTION
 *
 *	Prints error message using current process name, the reason for
 *	aborting (typically out of memory), and exits with status 1.
 *	This should probably be changed to use a status code
 *	defined in the user's debugger include file.
 *
 */

static void
DbugExit(const char *why)
{
    (void) fprintf(stderr, ERR_ABORT, _gu_db_process_, why);
    (void) fflush(stderr);
    exit(1);
}


/*
 *  FUNCTION
 *
 *	DbugMalloc    allocate memory for debugger runtime support
 *
 *  SYNOPSIS
 *
 *	static long *DbugMalloc (size)
 *	int size;
 *
 *  DESCRIPTION
 *
 *	Allocate more memory for debugger runtime support functions.
 *	Failure to to allocate the requested number of bytes is
 *	immediately fatal to the current process.  This may be
 *	rather unfriendly behavior.  It might be better to simply
 *	print a warning message, freeze the current debugger state,
 *	and continue execution.
 *
 */

static char *
DbugMalloc(int size)
{
    register char *new_malloc;

    if (!(new_malloc = (char *) malloc((unsigned int) size)))
	DbugExit("out of memory");
    return (new_malloc);
}


/*
 *		As strtok but two separators in a row are changed to one
 *		separator (to allow directory-paths in dos).
 */

static char *
static_strtok(char *s1, char separator)
{
    static char *end = NULL;
    register char *rtnval, *cpy;

    rtnval = NULL;
    if (s1 != NULL)
	end = s1;
    if (end != NULL && *end != EOS) {
	rtnval = cpy = end;
	do {
	    if ((*cpy++ = *end++) == separator) {
		if (*end != separator) {
		    cpy--;				   /* Point at separator */
		    break;
		}
		end++;					   /* Two separators in a row, skipp one */
	    }
	} while (*end != EOS);
	*cpy = EOS;					   /* Replace last separator */
    }
    return (rtnval);
}


/*
 *  FUNCTION
 *
 *	BaseName    strip leading pathname components from name
 *
 *  SYNOPSIS
 *
 *	static char *BaseName (pathname)
 *	char *pathname;
 *
 *  DESCRIPTION
 *
 *	Given pointer to a complete pathname, locates the base file
 *	name at the end of the pathname and returns a pointer to
 *	it.
 *
 */

static char *
BaseName(const char *pathname)
{
    register const char *base;

    base = strrchr(pathname, FN_LIBCHAR);
//    if (base++ == NullS) - this doesn't make sense
    if (NULL == base || '\0' == base[1])
	base = pathname;
    return ((char *) base);
}


/*
 *  FUNCTION
 *
 *	Writable    test to see if a pathname is writable/creatable
 *
 *  SYNOPSIS
 *
 *	static BOOLEAN Writable (pathname)
 *	char *pathname;
 *
 *  DESCRIPTION
 *
 *	Because the debugger might be linked in with a program that
 *	runs with the set-uid-bit (suid) set, we have to be careful
 *	about opening a user named file for debug output.  This consists
 *	of checking the file for write access with the real user id,
 *	or checking the directory where the file will be created.
 *
 *	Returns TRUE if the user would normally be allowed write or
 *	create access to the named file.  Returns FALSE otherwise.
 *
 */


#ifndef Writable

static BOOLEAN
Writable(char *pathname)
{
    REGISTER BOOLEAN granted;
    REGISTER char *lastslash;

    granted = FALSE;
    if (EXISTS(pathname)) {
	if (WRITABLE(pathname)) {
	    granted = TRUE;
	}
    } else {
	lastslash = strrchr(pathname, '/');
	if (lastslash != NULL) {
	    *lastslash = EOS;
	} else {
	    pathname = ".";
	}
	if (WRITABLE(pathname)) {
	    granted = TRUE;
	}
	if (lastslash != NULL) {
	    *lastslash = '/';
	}
    }
    return (granted);
}
#endif


/*
 *  FUNCTION
 *
 *	ChangeOwner    change owner to real user for suid programs
 *
 *  SYNOPSIS
 *
 *	static VOID ChangeOwner (pathname)
 *
 *  DESCRIPTION
 *
 *	For unix systems, change the owner of the newly created debug
 *	file to the real owner.  This is strictly for the benefit of
 *	programs that are running with the set-user-id bit set.
 *
 *	Note that at this point, the fact that pathname represents
 *	a newly created file has already been established.  If the
 *	program that the debugger is linked to is not running with
 *	the suid bit set, then this operation is redundant (but
 *	harmless).
 *
 */

#ifndef ChangeOwner
static void
ChangeOwner(char *pathname)
{
    if (chown(pathname, getuid(), getgid()) == -1) {
	(void) fprintf(stderr, ERR_CHOWN, _gu_db_process_, pathname);
	perror("");
	(void) fflush(stderr);
    }
}
#endif


/*
 *  FUNCTION
 *
 *	_gu_db_setjmp_    save debugger environment
 *
 *  SYNOPSIS
 *
 *	VOID _gu_db_setjmp_ ()
 *
 *  DESCRIPTION
 *
 *	Invoked as part of the user's GU_DBUG_SETJMP macro to save
 *	the debugger environment in parallel with saving the user's
 *	environment.
 *
 */

#ifdef HAVE_LONGJMP

void
_gu_db_setjmp_()
{
    CODE_STATE *state;
    state = code_state();

    state->jmplevel = state->level;
    state->jmpfunc = state->func;
    state->jmpfile = state->file;
}

/*
 *  FUNCTION
 *
 *	_gu_db_longjmp_	restore previously saved debugger environment
 *
 *  SYNOPSIS
 *
 *	VOID _gu_db_longjmp_ ()
 *
 *  DESCRIPTION
 *
 *	Invoked as part of the user's GU_DBUG_LONGJMP macro to restore
 *	the debugger environment in parallel with restoring the user's
 *	previously saved environment.
 *
 */

void
_gu_db_longjmp_()
{
    CODE_STATE *state;
    state = code_state();

    state->level = state->jmplevel;
    if (state->jmpfunc) {
	state->func = state->jmpfunc;
    }
    if (state->jmpfile) {
	state->file = state->jmpfile;
    }
}
#endif

/*
 *  FUNCTION
 *
 *	DelayArg   convert D flag argument to appropriate value
 *
 *  SYNOPSIS
 *
 *	static int DelayArg (value)
 *	int value;
 *
 *  DESCRIPTION
 *
 *	Converts delay argument, given in tenths of a second, to the
 *	appropriate numerical argument used by the system to delay
 *	that that many tenths of a second.  For example, on the
 *	amiga, there is a system call "Delay()" which takes an
 *	argument in ticks (50 per second).  On unix, the sleep
 *	command takes seconds.	Thus a value of "10", for one
 *	second of delay, gets converted to 50 on the amiga, and 1
 *	on unix.  Other systems will need to use a timing loop.
 *
 */

#ifdef AMIGA
#define HZ (50)						   /* Probably in some header somewhere */
#endif

static int
DelayArg(int value)
{
    uint delayarg = 0;

#if (unix || xenix)
    delayarg = value / 10;				   /* Delay is in seconds for sleep () */
#endif
#ifdef AMIGA
    delayarg = (HZ * value) / 10;			   /* Delay in ticks for Delay () */
#endif
    return (delayarg);
}


/*
 *	A dummy delay stub for systems that do not support delays.
 *	With a little work, this can be turned into a timing loop.
 */

#if ! defined(Delay) && ! defined(AMIGA)
static int
Delay(int ticks)
{
    return ticks;
}
#endif


/*
 *  FUNCTION
 *
 *	perror	  perror simulation for systems that don't have it
 *
 *  SYNOPSIS
 *
 *	static VOID perror (s)
 *	char *s;
 *
 *  DESCRIPTION
 *
 *	Perror produces a message on the standard error stream which
 *	provides more information about the library or system error
 *	just encountered.  The argument string s is printed, followed
 *	by a ':', a blank, and then a message and a newline.
 *
 *	An undocumented feature of the unix perror is that if the string
 *	's' is a null string (NOT a NULL pointer!), then the ':' and
 *	blank are not printed.
 *
 *	This version just complains about an "unknown system error".
 *
 */




	/* flush dbug-stream, free mutex lock & wait delay */
	/* This is because some systems (MSDOS!!) dosn't flush fileheader */
	/* and dbug-file isn't readable after a system crash !! */

static void
dbug_flush(CODE_STATE * state)
{
#ifndef THREAD
    if (_gu_db_stack->flags & FLUSH_ON_WRITE)
#endif
    {
#if defined(MSDOS) || defined(__WIN__)
	if (_gu_db_fp_ != stdout && _gu_db_fp_ != stderr) {
	    if (!(freopen(_gu_db_stack->name, "a", _gu_db_fp_))) {
		(void) fprintf(stderr, ERR_OPEN, _gu_db_process_,_gu_db_stack->name);
		fflush(stderr);
		_gu_db_fp_ = stdout;
		_gu_db_stack->out_file = _gu_db_fp_;
		_gu_db_stack->flags |= FLUSH_ON_WRITE;
	    }
	} else
#endif
	{
	    (void) fflush(_gu_db_fp_);
	    if (_gu_db_stack->delay)
		(void) Delay(_gu_db_stack->delay);
	}
    }
    if (!state || !state->locked)
	pthread_mutex_unlock(&_gu_db_mutex);
}							   /* dbug_flush */


void
_gu_db_lock_file()
{
    CODE_STATE *state;
    state = code_state();
    pthread_mutex_lock(&_gu_db_mutex);
    state->locked = 1;
}

void
_gu_db_unlock_file()
{
    CODE_STATE *state;
    state = code_state();
    state->locked = 0;
    pthread_mutex_unlock(&_gu_db_mutex);
}

/*
 * Here we need the definitions of the clock routine.  Add your
 * own for whatever system that you have.
 */

#ifndef THREAD
#if defined(HAVE_GETRUSAGE)

#include <sys/param.h>
#include <sys/resource.h>

/* extern int getrusage(int, struct rusage *); */

/*
 * Returns the user time in milliseconds used by this process so
 * far.
 */

static unsigned long
Clock()
{
    struct rusage ru;

    (void) getrusage(RUSAGE_SELF, &ru);
    return ((ru.ru_utime.tv_sec * 1000) + (ru.ru_utime.tv_usec / 1000));
}

#elif defined(MSDOS) || defined(__WIN__) || defined(OS2)

static ulong
Clock()
{
    return clock() * (1000 / Cmy_pthread_mutex_lockS_PER_SEC);
}
#elif defined (amiga)

struct DateStamp
{							   /* Yes, this is a hack, but doing it right */
    long ds_Days;					   /* is incredibly ugly without splitting this */
    long ds_Minute;					   /* off into a separate file */
    long ds_Tick;
};

static int first_clock = TRUE;
static struct DateStamp begin;
static struct DateStamp elapsed;

static unsigned long
Clock()
{
    register struct DateStamp *now;
    register unsigned long millisec = 0;
    extern VOID *AllocMem();

    now = (struct DateStamp *) AllocMem((long) sizeof(struct DateStamp), 0L);
    if (now != NULL) {
	if (first_clock == TRUE) {
	    first_clock = FALSE;
	    (void) DateStamp(now);
	    begin = *now;
	}
	(void) DateStamp(now);
	millisec = 24 * 3600 * (1000 / HZ) * (now->ds_Days - begin.ds_Days);
	millisec += 60 * (1000 / HZ) * (now->ds_Minute - begin.ds_Minute);
	millisec += (1000 / HZ) * (now->ds_Tick - begin.ds_Tick);
	(void) FreeMem(now, (long) sizeof(struct DateStamp));
    }
    return (millisec);
}
#else
static unsigned long
Clock()
{
    return (0);
}
#endif /* RUSAGE */
#endif /* THREADS */

#ifdef NO_VARARGS

/*
 *	Fake vfprintf for systems that don't support it.  If this
 *	doesn't work, you are probably SOL...
 */

static int
vfprintf(stream, format, ap)
     FILE *stream;
     char *format;
     va_list ap;
{
    int rtnval;
    ARGS_DCL;

    ARG0 = va_arg(ap, ARGS_TYPE);
    ARG1 = va_arg(ap, ARGS_TYPE);
    ARG2 = va_arg(ap, ARGS_TYPE);
    ARG3 = va_arg(ap, ARGS_TYPE);
    ARG4 = va_arg(ap, ARGS_TYPE);
    ARG5 = va_arg(ap, ARGS_TYPE);
    ARG6 = va_arg(ap, ARGS_TYPE);
    ARG7 = va_arg(ap, ARGS_TYPE);
    ARG8 = va_arg(ap, ARGS_TYPE);
    ARG9 = va_arg(ap, ARGS_TYPE);
    rtnval = fprintf(stream, format, ARGS_LIST);
    return (rtnval);
}

#endif /* NO_VARARGS */

char _gu_dig_vec[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
