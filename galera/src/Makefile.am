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