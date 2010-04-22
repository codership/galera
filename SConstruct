###################################################################
#
# Copyright (C) 2010 Codership Oy <info@codership.com>
#
# SCons build script to build galera libraries
#
# Script structure:
# - Help message
# - Default parameters 
# - Read commandline options
# - Set up and configure default build environment
# - Set up and configure check unit test build environment
# - Run root SConscript with variant_dir
#
####################################################################

import os

sysname = os.uname()[0].lower()
print sysname


#
# Print Help 
#

Help('''
Build targets:  build tests check install all
Default target: all
        
Commandline Options:
    debug=n       debug build with optimization level n
    arch=str      target architecture [i386|x86-64]
    build_dir=dir build directory, default .
    boost=[0|1]   disable or enable boost libraries
''')


#
# Default params
#

build_target = 'all'

# Optimization level
opt_flags    = '-g -O3 -DNDEBUG' 

# Architecture (defaults to build host type)
compile_arch = ''
link_arch    = ''

# Build directory
build_dir    = ''


#
# Read commandline options
#

build_dir = ARGUMENTS.get('build_dir', '')

# Debug flags
debug = ARGUMENTS.get('debug', -1)
if int(debug) >= 0:
    opt_flags = '-g -O{0}'.format(int(debug))

# Target arch
arch = ARGUMENTS.get('arch', '')
if arch == 'i386':
    compile_arch = '-m32'
    link_arch    = compile_arch + ' -m elf_i386'
elif arch == 'x86-64':
    compile_arch = '-m64'
    link_arch    = compile_arch + ' -m elf_x86_64'
        
boost = int(ARGUMENTS.get('boost', 1))


#
# Set up and export default build environment
#
# TODO: import env required for ccache and distcc 
#



env = DefaultEnvironment()

# Ports are installed under /usr/local 
if sysname == 'freebsd':
    env.Append(LIBPATH = '-L/usr/local/lib')
    env.Append(CPPFLAGS = '-I/usr/local/include')

#
# Set up build and link paths
# 

# Include paths
env.Append(CPPPATH = Split('''#/galerautils/src
                               #/gcomm/src
                               #/gcomm/src/gcomm
                               #/gcs/src
                               #/wsdb/src
                               #/galera/src
                               '''))

# Library paths
env.Append(LIBPATH = Split('''#/galerautils/src
                               #/gcomm/src
                               #/gcs/src
                               #/wsdb/src
                               #/galera/src
                               '''))

# Common C/CXX flags
# These should be kept minimal as they are appended after C/CXX specific flags
env.Replace(CCFLAGS = 
            opt_flags 
            + ' -Wall -Wextra -Werror -Wno-unused-parameter ' 
            + compile_arch)

# Linker flags  
# TODO: enable '-Wl,--warn-common -Wl,--fatal-warnings' after warnings from
# static linking have beed addressed
# 
env.Append(LINKFLAGS = ' ' + link_arch)

# CPPFLAGS
env.Append(CPPFLAGS = ' -D_XOPEN_SOURCE=600')

# CFLAGS
env.Replace(CFLAGS = '-std=c99 -fno-strict-aliasing -pedantic')

# CXXFLAGS
env.Replace(CXXFLAGS = 
            '-Wno-long-long -Wno-deprecated -Weffc++ -pedantic -ansi')




#
# Check required headers and libraries (autoconf functionality)
#

conf = Configure(env)

# System headers and libraries

if not conf.CheckLib('pthread'):
    print 'Error: pthread library not found'
    Exit(1)
    
if not conf.CheckLib('rt'):
    print 'Error: rt library not found'
    Exit(1)

if conf.CheckHeader('epoll.h'):
    conf.env.Append(CPPFLAGS = ' -DGALERA_USE_GU_NETWORK')

if conf.CheckHeader('byteswap.h'):
    conf.env.Append(CPPFLAGS = ' -DHAVE_BYTESWAP_H')

if conf.CheckHeader('endian.h'):
    conf.env.Append(CPPFLAGS = ' -DHAVE_ENDIAN_H')

if conf.CheckHeader('sys/endian.h'):
    conf.env.Append(CPPFLAGS = ' -DHAVE_SYS_ENDIAN_H')

# Additional C headers and libraries

if not conf.CheckCXXHeader('boost/shared_ptr.hpp'):
    print 'boost/shared_ptr.hpp not found or not usable, trying without -Weffc++'
    conf.env.Replace(CXXFLAGS = conf.env['CXXFLAGS'].replace('-Weffc++', ''))
    if not conf.CheckCXXHeader('boost/shared_ptr.hpp'):
        print 'boost/shared_ptr.hpp not found or not usable'
        Exit(1)

if boost == 1:
    # Use nanosecond time precision
    conf.env.Append(CPPFLAGS = ' -DBOOST_DATE_TIME_POSIX_TIME_STD_CONFIG=1')
    # Required boost headers/libraries
    # 
    if conf.CheckCXXHeader('boost/pool/pool_alloc.hpp'):
        print 'Using boost pool alloc'
        conf.env.Append(CPPFLAGS = ' -DGALERA_USE_BOOST_POOL_ALLOC=1')
    else:
        print 'Error: boost/pool/pool_alloc.hpp not found or not usable'

    
    if conf.CheckCXXHeader('boost/asio.hpp'):
        if conf.CheckLib('boost_system-mt'):
            print 'Using boost asio'
            conf.env.Append(CPPFLAGS = ' -DGALERA_USE_BOOST_ASIO=1')
        else:
            print 'Library boost_system-mt not usable'
else:
    print 'Not using boost'

env = conf.Finish()


#
# Set up and export environment for check unit tests
#

# Clone base from default environment
check_env = env.Clone()

conf = Configure(check_env)

# Check header and library

if not conf.CheckHeader('check.h'):
    print 'Error: check header file not found or not usable'
    Exit(1)

if not conf.CheckLib('check'):
    print 'Error: check library not found or not usable'
    Exit(1)

conf.Finish()

# Link unit tests statically
check_env.Append(LINKFLAGS = ' -static')

Export('check_env')


#
# Run root SConscript with variant_dir
#
SConscript('SConscript', variant_dir=build_dir)
