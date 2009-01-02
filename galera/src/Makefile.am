# 
lib_LTLIBRARIES = libgalera.la libdummy.la
noinst_PROGRAMS = test


libgalera_la_SOURCES = galera.c galera.h
libgalera_la_CFLAGS = -DGALERA_INTERFACE_VERSION=\"$(INTERFACE):$(REVISION):$(AGE)\"
INTERFACE = 1
REVISION  = 0
AGE       = 0
libgalera_la_LDFLAGS = -version-info $(INTERFACE):$(REVISION):$(AGE)

libdummy_la_SOURCES = dummy.c
libdummy_la_CFLAGS = 

test_SOURCES = test.c
test_LDADD = libgalera.la
test_LDFLAGS = -ldl


# <libgalera>
include_HEADERS =
lib_LTLIBRARIES += libmmgalera.la
libmmgalera_la_SOURCES = \
	mm_galera.c       


#GALERA_RELEASE =
#if ENABLE_RELEASE
#GALERA_RELEASE = -Wl,--whole-archive .libs/libwsdb.a -Wl,--no-whole-archive
#endif
#libmmgalera_la_DEPENDENCIES = libwsdb.la
libmmgalera_la_LIBADD       = -lwsdb
GALERA_INTERFACE = 5
GALERA_REVISION  = 0
GALERA_AGE       = 0
libmmgalera_la_LDFLAGS = \
	-version-info $(GALERA_INTERFACE):$(GALERA_REVISION):$(GALERA_AGE)
libmmgalera_la_CPPFLAGS = $(CPPFLAGS)
# </libgalera>
