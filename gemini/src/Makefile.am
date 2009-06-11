# Copyright (C) 2009 Codership Oy <info@codership.com>
#

GEMINI_SRCS = \
	Gemini.cpp

GEMINI_INCS = \
	Gemini.hpp

# Describe Gemini library target
include_HEADERS   = Gemini.hpp
lib_LTLIBRARIES   = libgemini.la
libgemini_la_SOURCES = $(GEMINI_SRCS) $(GEMINI_INCS)
INTERFACE = 1
REVISION  = 0
AGE       = 0
libgemini_la_LDFLAGS = \
	-version-info $(INTERFACE):$(REVISION):$(AGE)

# Desclibe gemini_test binary target
#noinst_PROGRAMS  = test
#test_SOURCES     = test.cpp
#test_LDADD       = $(lib_LTLIBRARIES)
#test_LDFLAGS     = -static

.PHONY: lib test tags

lib:  $(lib_LTLIBRARIES)

tags: TAGS

