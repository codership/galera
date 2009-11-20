# Copyright (C) 2007 Codership Oy <info@codership.com>
#  
# This file is free software; as a special exception the author gives
# unlimited permission to copy and/or distribute it, with or without 
# modifications, as long as this notice is preserved.
# 
# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY, to the extent permitted by law; without even the
# implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

dist_noinst_DATA = Doxyfile

.PHONY: docs clean

html/index.html: Doxyfile
	doxygen $<

docs: html/index.html

clean:
	rm -rf html man

