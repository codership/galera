###################################################################
#
# Copyright (C) 2010-2012 Codership Oy <info@codership.com>
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
import platform

sysname = os.uname()[0].lower()
machine = platform.machine()
print 'Host: ' + sysname + ' ' + machine

#
# Print Help
#

Help('''
Build targets:  build tests check install all
Default target: all

Commandline Options:
    debug=n          debug build with optimization level n
    arch=str         target architecture [i686|x86_64]
    build_dir=dir    build directory, default: '.'
    boost=[0|1]      disable or enable boost libraries
    boost_pool=[0|1] use or not use boost pool allocator
    revno=XXXX       source code revision number
    bpostatic=path   a path to static libboost_program_options.a
''')
# bpostatic option added on Percona request

#
# Default params
#

build_target = 'all'

# Optimization level
opt_flags    = ' -g -O3 -DNDEBUG'

# Architecture (defaults to build host type)
compile_arch = ''
link_arch    = ''

# Build directory
build_dir    = ''


#
# Read commandline options
#

build_dir = ARGUMENTS.get('build_dir', '')

# Debug/dbug flags
debug = ARGUMENTS.get('debug', -1)
dbug  = ARGUMENTS.get('dbug', False)

if int(debug) >= 0 and int(debug) < 3:
    opt_flags = ' -g -O%d -fno-inline' % int(debug)
    dbug = True
elif int(debug) == 3:
    opt_flags = ' -g -O3'

if dbug:
    opt_flags = opt_flags + ' -DGU_DBUG_ON'

# Target arch
arch = ARGUMENTS.get('arch', machine)
print 'Target: ' + sysname + ' ' + arch

if arch == 'i386' or arch == 'i686':
    compile_arch = ' -m32 -march=i686'
    link_arch    = compile_arch + ' -Wl,-melf_i386'
elif arch == 'x86_64' or arch == 'amd64':
    compile_arch = ' -m64'
    link_arch    = compile_arch + ' -Wl,-melf_x86_64'
elif arch == 'ppc64':
    compile_arch = ' -mtune=native'
    link_arch    = ''
elif sysname == 'sunos':
    compile_arch = ''
    link_arch    = ''
else:
    compile_arch = ''
    link_arch    = ''

boost      = int(ARGUMENTS.get('boost', 1))
boost_pool = int(ARGUMENTS.get('boost_pool', 1))
ssl        = int(ARGUMENTS.get('ssl', 1))

GALERA_VER = ARGUMENTS.get('version', '2.3')
GALERA_REV = ARGUMENTS.get('revno', 'XXXX')
# export to any module that might have use of those
Export('GALERA_VER', 'GALERA_REV')
print 'Signature: version: ' + GALERA_VER + ', revision: ' + GALERA_REV

LIBBOOST_PROGRAM_OPTIONS_A = ARGUMENTS.get('bpostatic', '');
Export('LIBBOOST_PROGRAM_OPTIONS_A')

#
# Set up and export default build environment
#

env = DefaultEnvironment()

# Set up environment for ccache and distcc
env['ENV']['HOME']          = os.environ['HOME']
#env['ENV']['DISTCC_HOSTS']  = os.environ['DISTCC_HOSTS']
#env['ENV']['CCACHE_PREFIX'] = os.environ['CCACHE_PREFIX']

# Set CC and CXX compilers
cc = os.getenv('CC', 'default')
if cc != 'default':
    env.Replace(CC = cc)
cxx = os.getenv('CXX', 'default')
if cxx != 'default':
    env.Replace(CXX = cxx)
link = os.getenv('LINK', 'default')
if link != 'default':
    env.Replace(LINK = link)

# Freebsd ports are installed under /usr/local 
if sysname == 'freebsd' or sysname == 'sunos':
    env.Append(LIBPATH  = ['/usr/local/lib'])
    env.Append(CPPFLAGS = ' -I/usr/local/include')

#
# Set up build and link paths
# 

# Include paths
env.Append(CPPPATH = Split('''#/common
                              #/asio
                              #/galerautils/src
                              #/gcomm/src
                              #/gcomm/src/gcomm
                              #/gcache/src
                              #/gcs/src
                              #/wsdb/src
                              #/galera/src
                           '''))

# Library paths
#env.Append(LIBPATH = Split('''#/galerautils/src
#                              #/gcomm/src
#                              #/gcs/src
#                              #/wsdb/src
#                              #/galera/src
#                           '''))

# Preprocessor flags
if sysname != 'sunos':
    env.Append(CPPFLAGS = ' -D_XOPEN_SOURCE=600')
else:
    env.Append(CPPFLAGS = ' -D__EXTENSIONS__')
env.Append(CPPFLAGS = ' -DHAVE_COMMON_H')

# Common C/CXX flags
# These should be kept minimal as they are appended after C/CXX specific flags
env.Replace(CCFLAGS = opt_flags + compile_arch +
                      ' -Wall -Wextra -Werror -Wno-unused-parameter')

# C-specific flags
env.Replace(CFLAGS = ' -std=c99 -fno-strict-aliasing -pipe')

# CXX-specific flags
# Note: not all 3rd-party libs like '-Wold-style-cast -Weffc++'
#       adding those after checks
env.Replace(CXXFLAGS = ' -Wno-long-long -Wno-deprecated -ansi')
if sysname != 'sunos':
    env.Append(CXXFLAGS = ' -pipe')


# Linker flags
# TODO: enable '-Wl,--warn-common -Wl,--fatal-warnings' after warnings from
# static linking have beed addressed
#
env.Append(LINKFLAGS = link_arch)

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

if sysname == 'sunos':
    if not conf.CheckLib('socket'):
        print 'Error: socket library not found'
        Exit(1)
    if not conf.CheckLib('crypto'):
        print 'Error: crypto library not found'
        Exit(1)
    if not conf.CheckLib('nsl'):
        print 'Error: nsl library not found'
        Exit(1)

if conf.CheckHeader('sys/epoll.h'):
    conf.env.Append(CPPFLAGS = ' -DGALERA_USE_GU_NETWORK')

if conf.CheckHeader('byteswap.h'):
    conf.env.Append(CPPFLAGS = ' -DHAVE_BYTESWAP_H')

if conf.CheckHeader('endian.h'):
    conf.env.Append(CPPFLAGS = ' -DHAVE_ENDIAN_H')
elif conf.CheckHeader('sys/endian.h'):
    conf.env.Append(CPPFLAGS = ' -DHAVE_SYS_ENDIAN_H')
elif conf.CheckHeader('sys/byteorder.h'):
    conf.env.Append(CPPFLAGS = ' -DHAVE_SYS_BYTEORDER_H')
else:
    print 'can\'t find byte order information'
    Exit(1)

# Additional C headers and libraries

# boost headers

if not conf.CheckCXXHeader('boost/shared_ptr.hpp'):
    print 'boost/shared_ptr.hpp not found or not usable'
    Exit(1)
conf.env.Append(CPPFLAGS = ' -DHAVE_BOOST_SHARED_PTR_HPP')

if conf.CheckCXXHeader('tr1/unordered_map'):
    conf.env.Append(CPPFLAGS = ' -DHAVE_TR1_UNORDERED_MAP')
else:
    if conf.CheckCXXHeader('boost/unordered_map.hpp'):
        conf.env.Append(CPPFLAGS = ' -DHAVE_BOOST_UNORDERED_MAP_HPP')
    else:
        print 'no unordered map header available'
        Exit(1)

# pool allocator
if boost == 1:
    # Use nanosecond time precision
    conf.env.Append(CPPFLAGS = ' -DBOOST_DATE_TIME_POSIX_TIME_STD_CONFIG=1')
    # Required boost headers/libraries
    #
    if boost_pool == 1:
        if conf.CheckCXXHeader('boost/pool/pool_alloc.hpp'):
            print 'Using boost pool alloc'
            conf.env.Append(CPPFLAGS = ' -DGALERA_USE_BOOST_POOL_ALLOC=1')
            # due to a bug in boost >= 1.50 we need to link with boost_system
            # - should be a noop with no boost_pool.
#            if conf.CheckLib('boost_system'):
#        	conf.env.Append(LIBS=['boost_system'])
        else:
            print 'Error: boost/pool/pool_alloc.hpp not found or not usable'
            Exit(1)
else:
    print 'Not using boost'

# asio
if conf.CheckCXXHeader('asio.hpp'):
    conf.env.Append(CPPFLAGS = ' -DHAVE_ASIO_HPP')
else:
    print 'asio headers not found or not usable'
    Exit(1)

# asio/ssl
if ssl == 1:
    if conf.CheckCXXHeader('asio/ssl.hpp'):
        conf.env.Append(CPPFLAGS = ' -DHAVE_ASIO_SSL_HPP')
    else:
        print 'ssl support required but asio/ssl.hpp not found or not usable'
        print 'compile with ssl=0 or check that openssl devel headers are usable'
        Exit(1)
    if conf.CheckLib('ssl'):
        conf.CheckLib('crypto')
    else:
        print 'ssl support required but openssl library not found'
        print 'compile with ssl=0 or check that openssl library is usable'
        Exit(1)

# these will be used only with our softaware
conf.env.Append(CCFLAGS  = ' -pedantic')
conf.env.Append(CXXFLAGS = ' -Weffc++ -Wold-style-cast')

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

# Note: Don't do this, glibc does not like static linking
# Link unit tests statically
# check_env.Append(LINKFLAGS = ' -static')

#
# this follows recipes from http://www.scons.org/wiki/UnitTests
#

def builder_unit_test(target, source, env):
    app = str(source[0].abspath)
    if os.spawnl(os.P_WAIT, app, app)==0:
        open(str(target[0]),'w').write("PASSED\n")
    else:
        return 1
# Create a builder for tests
bld = Builder(action = builder_unit_test)
check_env.Append(BUILDERS = {'Test' :  bld})

Export('check_env')

#
# Run root SConscript with variant_dir
#
SConscript('SConscript', variant_dir=build_dir)
