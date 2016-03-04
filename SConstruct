###################################################################
#
# Copyright (C) 2010-2015 Codership Oy <info@codership.com>
#
# SCons build script to build galera libraries
#
# How to control the build with environment variables:
# Set CC       to specify C compiler
# Set CXX      to specify C++ compiler
# Set CPPFLAGS to add non-standard include paths and preprocessor macros
# Set CCFLAGS  to *override* optimization and architecture-specific options
# Set CFLAGS   to supply C compiler options
# Set CXXFLAGS to supply C++ compiler options
# Set LDFLAGS  to *override* linking flags
# Set LIBPATH  to add non-standard linker paths
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
bits = ARGUMENTS.get('bits', platform.architecture()[0])
print 'Host: ' + sysname + ' ' + machine + ' ' + bits

x86 = any(arch in machine for arch in [ 'x86', 'amd64', 'i686', 'i386' ])

if bits == '32bit':
    bits = 32
elif bits == '64bit':
    bits = 64

#
# Print Help
#

Help('''
Build targets:  build tests check install all
Default target: all

Commandline Options:
    debug=n             debug build with optimization level n
    build_dir=dir       build directory, default: '.'
    boost=[0|1]         disable or enable boost libraries
    boost_pool=[0|1]    use or not use boost pool allocator
    revno=XXXX          source code revision number
    bpostatic=path      a path to static libboost_program_options.a
    extra_sysroot=path  a path to extra development environment (Fink, Homebrew, MacPorts, MinGW)
    bits=[32bit|64bit]
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

if sysname == 'sunos':
    compile_arch = ' -mtune=native'
elif x86:
    if bits == 32:
        if machine == 'x86_64':
            compile_arch = ' -mx32'
        else:
            compile_arch = ' -m32 -march=i686'
            if sysname == 'linux':
                link_arch = ' -Wl,-melf_i386'
    else:
        compile_arch = ' -m64'
        if sysname == 'linux':
            link_arch = ' -Wl,-melf_x86_64'
    link_arch = compile_arch + link_arch
elif machine == 's390x':
    compile_arch = ' -mzarch'
    if bits == 32:
        compile_arch += ' -m32'

boost      = int(ARGUMENTS.get('boost', 1))
boost_pool = int(ARGUMENTS.get('boost_pool', 0))
ssl        = int(ARGUMENTS.get('ssl', 1))
tests      = int(ARGUMENTS.get('tests', 1))
strict_build_flags = int(ARGUMENTS.get('strict_build_flags', 1))


GALERA_VER = ARGUMENTS.get('version', '3.15')
GALERA_REV = ARGUMENTS.get('revno', 'XXXX')

# Attempt to read from file if not given
if GALERA_REV == "XXXX" and os.path.isfile("GALERA_REVISION"):
    with open("GALERA_REVISION", "r") as f:
        GALERA_REV = f.readline().rstrip("\n")

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
env.Replace(CPPFLAGS  = os.getenv('CPPFLAGS', ''))
env.Replace(CCFLAGS   = os.getenv('CCFLAGS',  opt_flags + compile_arch))
env.Replace(CFLAGS    = os.getenv('CFLAGS',   ''))
env.Replace(CXXFLAGS  = os.getenv('CXXFLAGS', ''))
env.Replace(LINKFLAGS = os.getenv('LDFLAGS',  link_arch))
env.Replace(LIBPATH   = [os.getenv('LIBPATH', '')])

# Set -pthread flag explicitly to make sure that pthreads are
# enabled on all platforms.
env.Append(CCFLAGS = ' -pthread')

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

# Preprocessor flags
if sysname != 'sunos' and sysname != 'darwin' and sysname != 'freebsd':
    env.Append(CPPFLAGS = ' -D_XOPEN_SOURCE=600')
if sysname == 'sunos':
    env.Append(CPPFLAGS = ' -D__EXTENSIONS__')
env.Append(CPPFLAGS = ' -DHAVE_COMMON_H')

# Common C/CXX flags
# These should be kept minimal as they are appended after C/CXX specific flags
env.Append(CCFLAGS = ' -Wall -Wextra -Wno-unused-parameter')

# C-specific flags
env.Append(CFLAGS = ' -std=c99 -fno-strict-aliasing -pipe')

# CXX-specific flags
# Note: not all 3rd-party libs like '-Wold-style-cast -Weffc++'
#       adding those after checks
env.Append(CXXFLAGS = ' -Wno-long-long -Wno-deprecated -ansi')
if sysname != 'sunos':
    env.Append(CXXFLAGS = ' -pipe')


# Linker flags
# TODO: enable ' -Wl,--warn-common -Wl,--fatal-warnings' after warnings from
# static linking have beed addressed
#
#env.Append(LINKFLAGS = ' -Wl,--warn-common -Wl,--fatal-warnings')

#
# Check required headers and libraries (autoconf functionality)
#

#
# Custom tests:
#

def CheckSystemASIOVersion(context):
    system_asio_test_source_file = """
#include <asio.hpp>

#if ASIO_VERSION < 101001
#error "Included asio version is too old"
#endif

int main()
{
    return 0;
}

"""
    context.Message('Checking ASIO version (> 1.10.1) ... ')
    result = context.TryLink(system_asio_test_source_file, '.cpp')
    context.Result(result)
    return result


#
# Construct confuration context
#
conf = Configure(env, custom_tests = {'CheckSystemASIOVersion': CheckSystemASIOVersion})

# System headers and libraries

if not conf.CheckLib('pthread'):
    print 'Error: pthread library not found'
    Exit(1)

# libatomic may be needed on some 32bit platforms (and 32bit userland PPC64)
# for 8 byte atomics but not generally required
if not x86:
    conf.CheckLib('atomic')

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

if conf.CheckHeader('execinfo.h'):
    conf.env.Append(CPPFLAGS = ' -DHAVE_EXECINFO_H')

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
    if bits == 64:
        boost_libpaths = [ boost_library_path, '/usr/lib64', '/usr/local/lib64' ]
    else:
        boost_libpaths = [ boost_library_path, '/usr/local/lib', '/usr/lib' ]

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
use_system_asio = False
if conf.CheckCXXHeader('asio.hpp') and conf.CheckSystemASIOVersion():
    use_system_asio = True
    conf.env.Append(CPPFLAGS = ' -DHAVE_SYSTEM_ASIO -DHAVE_ASIO_HPP')
else:
    print "Falling back to bundled asio"

if not use_system_asio:
    # Fall back to embedded asio
    conf.env.Append(CPPPATH = [ '#/asio' ])
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
    conf.env.Append(CCFLAGS = ' -Werror -pedantic')
    if 'clang' not in conf.env['CXX']:
        conf.env.Append(CXXFLAGS = ' -Weffc++ -Wold-style-cast')
    else:
        conf.env.Append(CCFLAGS = ' -Wno-self-assign')
        if 'ccache' in conf.env['CXX']:
            conf.env.Append(CCFLAGS = ' -Qunused-arguments')

env = conf.Finish()

Export('x86', 'bits', 'env', 'sysname', 'libboost_program_options')

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

# potential check dependency, link if present
conf.CheckLib('subunit')

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
