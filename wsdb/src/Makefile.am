# Copyright (C) 2007 Codership Oy <info@codership.com>
# Initialise libtool variables
include_HEADERS = 
lib_LTLIBRARIES = 
noinst_LTLIBRARIES = 

# Describe dumpwsdb binary target
noinst_PROGRAMS  = dumpwsdb
dumpwsdb_SOURCES = dump_trxfile.c
dumpwsdb_LDADD   = $(lib_LTLIBRARIES)
dumpwsdb_LDFLAGS = -static

# <libwsdb>
include_HEADERS += wsdb_api.h
#noinst_LTLIBRARIES += libwsdb.la
lib_LTLIBRARIES += libwsdb.la
libwsdb_la_SOURCES =    \
	certification.c \
	file.c          \
	file_cache.c    \
	hash.c          \
	local.c         \
	version_file.c  \
	wsdb.c          \
	wsdb_xdr.c	\
	file.h          \
	file_cache.h    \
	hash.h          \
	conn.c          \
	conn.h          \
	key_array.c     \
	key_array.h     \
	version_file.h  \
	wsdb_api.h      \
	wsdb_priv.h     \
	wsdb_file.h     \
	job_queue.c     \
	job_queue.h     \
	wsdb_xdr.h      

WSDB_INTERFACE = 1
WSDB_REVISION  = 0
WSDB_AGE       = 0
libwsdb_la_LDFLAGS = \
	-Wl,--whole-archive -lgalerautils -Wl,--no-whole-archive \
	-version-info $(WSDB_INTERFACE):$(WSDB_REVISION):$(WSDB_AGE)
libwsdb_la_CFLAGS = $(CFLAGS) -fno-strict-aliasing
# </libwsdb>

# <libgalera>
include_HEADERS += galera.h
lib_LTLIBRARIES += libgalera.la
libgalera_la_SOURCES = \
	galera.c       \
	galera.h

#GALERA_RELEASE =
#if ENABLE_RELEASE
#GALERA_RELEASE = -Wl,--whole-archive .libs/libwsdb.a -Wl,--no-whole-archive
#endif
libgalera_la_DEPENDENCIES = libwsdb.la
libgalera_la_LIBADD       = libwsdb.la
GALERA_INTERFACE = 3
GALERA_REVISION  = 0
GALERA_AGE       = 0
libgalera_la_LDFLAGS = \
	-version-info $(GALERA_INTERFACE):$(GALERA_REVISION):$(GALERA_AGE)

# </libgalera>

#.PHONY: tags dumpwsdb
.PHONY: tags clean
tags: TAGS

#dumpwsdb: dumpwsdb
#
