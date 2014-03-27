###################################################################
#
# Copyright (C) 2010-2014 Codership Oy <info@codership.com>
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
import string

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
    debug=n             debug build with optimization level n
    arch=str            target architecture [i686|x86_64]
    build_dir=dir       build directory, default: '.'
    boost=[0|1]         disable or enable boost libraries
    boost_pool=[0|1]    use or not use boost pool allocator
    revno=XXXX          source code revision number
    bpostatic=path      a path to static libboost_program_options.a
    extra_sysroot=path  a path to extra development environment (Fink, Homebrew, MacPorts, MinGW)
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

debug_lvl = int(debug)
if debug_lvl >= 0 and debug_lvl < 3:
    opt_flags = ' -g -O%d -fno-inline' % debug_lvl
    dbug = True
elif debug_lvl == 3:
    opt_flags = ' -g -O3'

if dbug:
    opt_flags = opt_flags + ' -DGU_DBUG_ON'

# Target arch
arch = ARGUMENTS.get('arch', machine)
print 'Target: ' + sysname + ' ' + arch

if arch == 'i386' or arch == 'i686':
    x86 = 32
elif arch == 'x86_64' or arch == 'amd64' or arch == 'i86pc':
    x86 = 64
else:
    x86 = 0

if x86 == 32:
    compile_arch = ' -m32 -march=i686'
    link_arch    = compile_arch
    if sysname == 'linux':
        link_arch = link_arch + ' -Wl,-melf_i386'
elif x86 == 64 and sysname != 'sunos':
    compile_arch = ' -m64'
    link_arch    = compile_arch
    if sysname == 'linux':
        link_arch = link_arch + ' -Wl,-melf_x86_64'
elif arch == 'ppc64':
    compile_arch = ' -mtune=native'
    link_arch    = ''
elif sysname == 'sunos':
    compile_arch = ' -mtune=native'
    link_arch    = ''
else:
    compile_arch = ''
    link_arch    = ''

boost      = int(ARGUMENTS.get('boost', 1))
boost_pool = int(ARGUMENTS.get('boost_pool', 0))
ssl        = int(ARGUMENTS.get('ssl', 1))
tests      = int(ARGUMENTS.get('tests', 1))
strict_build_flags = int(ARGUMENTS.get('strict_build_flags', 1))

GALERA_VER = ARGUMENTS.get('version', '3.5')
GALERA_REV = ARGUMENTS.get('revno', 'XXXX')
# export to any module that might have use of those
Export('GALERA_VER', 'GALERA_REV')
print 'Signature: version: ' + GALERA_VER + ', revision: ' + GALERA_REV

LIBBOOST_PROGRAM_OPTIONS_A = ARGUMENTS.get('bpostatic', '')
LIBBOOST_SYSTEM_A = string.replace(LIBBOOST_PROGRAM_OPTIONS_A, 'boost_program_options', 'boost_system')

#
# Set up and export default build environment
#

env = Environment(ENV = {'PATH' : os.environ['PATH'], 'HOME' : os.environ['HOME']})

# Set up environment for ccache and distcc
#env['ENV']['HOME']          = os.environ['HOME']
#env['ENV']['DISTCC_HOSTS']  = os.environ['DISTCC_HOSTS']
#env['ENV']['CCACHE_PREFIX'] = os.environ['CCACHE_PREFIX']
if 'CCACHE_DIR' in os.environ:
    env['ENV']['CCACHE_DIR'] = os.environ['CCACHE_DIR']
if 'CCACHE_CPP2' in os.environ:
    env['ENV']['CCACHE_CPP2'] = os.environ['CCACHE_CPP2']

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

# Initialize CPPFLAGS and LIBPATH from environment to get user preferences
env.Replace(CPPFLAGS = os.getenv('CPPFLAGS', ''))
env.Replace(LIBPATH = [os.getenv('LIBPATH', '')])

# Set -pthread flag explicitly to make sure that pthreads are
# enabled on all platforms.
env.Append(CPPFLAGS = ' -pthread')

# Freebsd ports are installed under /usr/local
if sysname == 'freebsd' or sysname == 'sunos':
    env.Append(LIBPATH  = ['/usr/local/lib'])
    env.Append(CPPFLAGS = ' -I/usr/local/include ')
if sysname == 'sunos':
   env.Replace(SHLINKFLAGS = '-shared ')

# Add paths is extra_sysroot argument was specified
extra_sysroot = ARGUMENTS.get('extra_sysroot', '')
if sysname == 'darwin' and extra_sysroot == '':
    # common developer environments and paths
    if os.system('which -s port') == 0 and os.path.isfile('/opt/local/bin/port'):
        extra_sysroot = '/opt/local'
    elif os.system('which -s brew') == 0 and os.path.isfile('/usr/local/bin/brew'):
        extra_sysroot = '/usr/local'
    elif os.system('which -s fink') == 0 and os.path.isfile('/sw/bin/fink'):
        extra_sysroot = '/sw'
if extra_sysroot != '':
    env.Append(LIBPATH = [extra_sysroot + '/lib'])
    env.Append(CPPFLAGS = ' -I' + extra_sysroot + '/include')

# print env.Dump()
#
# Set up build and link paths
#

# Include paths
env.Append(CPPPATH = Split('''#
                              #/asio
                              #/common
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
if sysname != 'sunos' and sysname != 'darwin' and sysname != 'freebsd':
    env.Append(CPPFLAGS = ' -D_XOPEN_SOURCE=600')
if sysname == 'sunos':
    env.Append(CPPFLAGS = ' -D__EXTENSIONS__')
env.Append(CPPFLAGS = ' -DHAVE_COMMON_H')

# Common C/CXX flags
# These should be kept minimal as they are appended after C/CXX specific flags
env.Replace(CCFLAGS = opt_flags + compile_arch +
                      ' -Wall -Wextra -Wno-unused-parameter')

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

if sysname != 'darwin':
    if not conf.CheckLib('rt'):
        print 'Error: rt library not found'
        Exit(1)

if sysname == 'freebsd':
    if not conf.CheckLib('execinfo'):
        print 'Error: execinfo library not found'
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
elif sysname != 'darwin':
    print 'can\'t find byte order information'
    Exit(1)

# Additional C headers and libraries

# boost headers

if not conf.CheckCXXHeader('boost/shared_ptr.hpp'):
    print 'boost/shared_ptr.hpp not found or not usable'
    Exit(1)
conf.env.Append(CPPFLAGS = ' -DHAVE_BOOST_SHARED_PTR_HPP')

if conf.CheckCXXHeader('unordered_map'):
    conf.env.Append(CPPFLAGS = ' -DHAVE_UNORDERED_MAP')
elif conf.CheckCXXHeader('tr1/unordered_map'):
    conf.env.Append(CPPFLAGS = ' -DHAVE_TR1_UNORDERED_MAP')
else:
    if conf.CheckCXXHeader('boost/unordered_map.hpp'):
        conf.env.Append(CPPFLAGS = ' -DHAVE_BOOST_UNORDERED_MAP_HPP')
    else:
        print 'no unordered map header available'
        Exit(1)

# pool allocator
if boost == 1:
    # Default suffix for boost multi-threaded libraries
    if sysname == 'darwin':
        boost_library_suffix = '-mt'
    else:
        boost_library_suffix = ''
    if sysname == 'darwin' and extra_sysroot != '':
        boost_library_path = extra_sysroot + '/lib'
    else:
        boost_library_path = ''
    # Use nanosecond time precision
    conf.env.Append(CPPFLAGS = ' -DBOOST_DATE_TIME_POSIX_TIME_STD_CONFIG=1')
    # Common procedure to find boost static library
    boost_libpaths = [ boost_library_path, '/usr/local/lib', '/usr/local/lib64', '/usr/lib', '/usr/lib64' ]
    def check_boost_library(libBaseName, header, configuredLibPath, autoadd = 1):
        libName = libBaseName + boost_library_suffix
        if configuredLibPath != '' and not os.path.isfile(configuredLibPath):
            print "Error: file '%s' does not exist" % configuredLibPath
            Exit(1)
        if configuredLibPath == '':
           for libpath in boost_libpaths:
               libname = libpath + '/lib%s.a' % libName
               if os.path.isfile(libname):
                   configuredLibPath = libname
                   break
        if configuredLibPath != '':
            if not conf.CheckCXXHeader(header):
                print "Error: header '%s' does not exist" % header
                Exit (1)
            if autoadd:
                conf.env.Append(LIBS=File(configuredLibPath))
            else:
                return File(configuredLibPath)
        else:
            if not conf.CheckLibWithHeader(libs=[libName],
                                           header=header,
                                           language='CXX',
                                           autoadd=autoadd):
                print 'Error: library %s does not exist' % libName
                Exit (1)
            return [libName]
    # Required boost headers/libraries
    #
    if boost_pool == 1:
        if conf.CheckCXXHeader('boost/pool/pool_alloc.hpp'):
            print 'Using boost pool alloc'
            conf.env.Append(CPPFLAGS = ' -DGALERA_USE_BOOST_POOL_ALLOC=1')
            # due to a bug in boost >= 1.50 we need to link with boost_system
            # - should be a noop with no boost_pool.
            if sysname == 'darwin':
                if conf.CheckLib('boost_system' + boost_library_suffix):
                    conf.env.Append(LIBS=['boost_system' + boost_library_suffix])
            check_boost_library('boost_system',
                                'boost/system/error_code.hpp',
                                LIBBOOST_SYSTEM_A)
        else:
            print 'Error: boost/pool/pool_alloc.hpp not found or not usable'
            Exit(1)

    libboost_program_options = check_boost_library('boost_program_options',
                                                   'boost/program_options.hpp',
                                                   LIBBOOST_PROGRAM_OPTIONS_A,
                                                   autoadd = 0)
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
if strict_build_flags == 1:
    conf.env.Append(CPPFLAGS = ' -Werror')
    conf.env.Append(CCFLAGS  = ' -pedantic')
    conf.env.Append(CXXFLAGS = ' -Weffc++ -Wold-style-cast')

env = conf.Finish()
Export('arch', 'x86', 'env', 'sysname', 'libboost_program_options')

#
# Actions to build .dSYM directories, containing debugging information for Darwin
#

if sysname == 'darwin' and int(debug) >= 0 and int(debug) < 3:
    env['LINKCOM'] = [env['LINKCOM'], 'dsymutil $TARGET']
    env['SHLINKCOM'] = [env['SHLINKCOM'], 'dsymutil $TARGET']

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

if not conf.CheckLib('m'):
    print 'Error: math library not found or not usable'
    Exit(1)

if sysname != 'darwin':
    if not conf.CheckLib('rt'):
        print 'Error: realtime library not found or not usable'
        Exit(1)

conf.Finish()

#
# this follows recipes from http://www.scons.org/wiki/UnitTests
#

def builder_unit_test(target, source, env):
    app = str(source[0].abspath)
    if os.spawnl(os.P_WAIT, app, app)==0:
        open(str(target[0]),'w').write("PASSED\n")
    else:
        return 1

def builder_unit_test_dummy(target, source, env):
    return 0

# Create a builder for tests
if tests == 1:
    bld = Builder(action = builder_unit_test)
else:
    bld = Builder(action = builder_unit_test_dummy) 
check_env.Append(BUILDERS = {'Test' :  bld})

Export('check_env')

#
# Run root SConscript with variant_dir
#
SConscript('SConscript', variant_dir=build_dir)
