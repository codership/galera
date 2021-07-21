###################################################################
#
# Copyright (C) 2010-2020 Codership Oy <info@codership.com>
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
# Set RPATH    to add rpaths
#
# Some useful CPPFLAGS:
# GCS_SM_DEBUG          - enable dumping of send monitor state and history
# GU_DEBUG_MUTEX        - enable mutex debug instrumentation
# GU_DBUG_ON            - enable sync point macros
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
import subprocess

# Execute a command and read the first line of its stdout.
# For example read_first_line(["ls", "-l", "/usr"])
def read_first_line(cmd):
    p = subprocess.Popen(cmd, stdout=subprocess.PIPE)
    stdout = p.communicate()[0]
    line = stdout.splitlines()[0]
    return line

sysname = os.uname()[0].lower()
machine = platform.machine()
bits = ARGUMENTS.get('bits', platform.architecture()[0])
print('Host: ' + sysname + ' ' + machine + ' ' + bits)

x86 = any(arch in machine for arch in [ 'x86', 'amd64', 'i686', 'i386', 'i86pc' ])

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
    asan=[0|1]          disable or enable ASAN instrumentation
    build_dir=dir       build directory, default: '.'
    boost=[0|1]         disable or enable boost libraries
    system_asio=[0|1]   use system asio library, if available
    boost_pool=[0|1]    use or not use boost pool allocator
    revno=XXXX          source code revision number
    bpostatic=path      a path to static libboost_program_options.a
    static_ssl=path     a path to static SSL libraries
    extra_sysroot=path  a path to extra development environment (Fink, Homebrew, MacPorts, MinGW)
    bits=[32bit|64bit]
    install=path        install files under path
    version_script=[0|1] Use version script (default 1)
    crc32c_no_hardware=[0|1] disable building hardware support for CRC32C
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

# Version script file
galera_script = File('#/galera/src/galera-sym.map').abspath

#
# Read commandline options
#

build_dir = ARGUMENTS.get('build_dir', '')

# Debug/dbug flags
debug = ARGUMENTS.get('debug', -1)
dbug  = ARGUMENTS.get('dbug', False)
asan = ARGUMENTS.get('asan', 0)

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
system_asio= int(ARGUMENTS.get('system_asio', 1))
tests      = int(ARGUMENTS.get('tests', 1))
# Run only tests which are known to be deterministic
deterministic_tests = int(ARGUMENTS.get('deterministic_tests', 0))
# Run all tests
all_tests = int(ARGUMENTS.get('all_tests', 0))
strict_build_flags = int(ARGUMENTS.get('strict_build_flags', 0))
static_ssl = ARGUMENTS.get('static_ssl', None)
install = ARGUMENTS.get('install', None)
version_script = int(ARGUMENTS.get('version_script', 1))

GALERA_VER = ARGUMENTS.get('version', '3.34')
GALERA_REV = ARGUMENTS.get('revno', 'XXXX')

# Attempt to read from file if not given
if GALERA_REV == "XXXX" and os.path.isfile("GALERA_REVISION"):
    with open("GALERA_REVISION", "r") as f:
        GALERA_REV = f.readline().rstrip("\n")

# export to any module that might have use of those
Export('GALERA_VER', 'GALERA_REV')
print('Signature: version: ' + GALERA_VER + ', revision: ' + GALERA_REV)

LIBBOOST_PROGRAM_OPTIONS_A = ARGUMENTS.get('bpostatic', '')
LIBBOOST_SYSTEM_A = LIBBOOST_PROGRAM_OPTIONS_A.replace('boost_program_options', 'boost_system')

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

# Get compiler name/version, CXX may be set to "c++" which may be clang or gcc
cc_version = str(read_first_line(env['CC'].split() + ['--version']))
cxx_version = str(read_first_line(env['CXX'].split() + ['--version']))

print('Using C compiler executable: ' + env['CC'])
print('C compiler version is: ' + cc_version)
print('Using C++ compiler executable: ' + env['CXX'])
print('C++ compiler version is: ' + cxx_version)

# Initialize CPPFLAGS and LIBPATH from environment to get user preferences
env.Replace(CPPFLAGS  = os.getenv('CPPFLAGS', ''))
env.Replace(CCFLAGS   = os.getenv('CCFLAGS',  opt_flags + compile_arch))
env.Replace(CFLAGS    = os.getenv('CFLAGS',   ''))
env.Replace(CXXFLAGS  = os.getenv('CXXFLAGS', ''))
env.Replace(LINKFLAGS = os.getenv('LDFLAGS',  link_arch))
env.Replace(LIBPATH   = [os.getenv('LIBPATH', '')])
env.Replace(RPATH     = [os.getenv('RPATH',   '')])

# Set -pthread flag explicitly to make sure that pthreads are
# enabled on all platforms.
env.Append(CCFLAGS = ' -pthread')

# FreeBSD ports are usually installed under /usr/local
if sysname == 'freebsd' or sysname == 'sunos':
    env.Append(LIBPATH = ['/usr/local/lib'])
    env.Append(CPPPATH = ['/usr/local/include'])
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
env.Append(CCFLAGS = ' -fPIC -Wall -Wextra -Wno-unused-parameter')

# C-specific flags
env.Prepend(CFLAGS = '-std=c99 -fno-strict-aliasing -pipe ')

# CXX-specific flags
# Note: not all 3rd-party libs like '-Wold-style-cast -Weffc++'
#       adding those after checks
env.Prepend(CXXFLAGS = '-Wno-long-long -Wno-deprecated -ansi ')
if sysname != 'sunos':
    env.Prepend(CXXFLAGS = '-pipe ')


# Linker flags
# TODO: enable ' -Wl,--warn-common -Wl,--fatal-warnings' after warnings from
# static linking have beed addressed
#
#env.Prepend(LINKFLAGS = '-Wl,--warn-common -Wl,--fatal-warnings ')

if int(asan):
    env.Append(CCFLAGS = ' -fsanitize=address')
    env.Append(CPPFLAGS = ' -DGALERA_WITH_ASAN')
    env.Append(LINKFLAGS = ' -fsanitize=address')

#
# Check required headers and libraries (autoconf functionality)
#

#
# Custom tests:
#

def CheckCpp11(context):
    test_source = """
#if __cplusplus < 201103
#error Not compiling in C++11 mode
#endif
int main() { return 0; }
"""
    context.Message('Checking if compiling in C++11 mode ... ')
    result = context.TryLink(test_source, '.cpp')
    context.Result(result)
    return result

def CheckSystemASIOVersion(context):
    system_asio_test_source_file = """
#include <asio.hpp>

#define XSTR(x) STR(x)
#define STR(x) #x
#pragma message "Asio version:" XSTR(ASIO_VERSION)
#if ASIO_VERSION < 101008
#error Included asio version is too old
#elif ASIO_VERSION >= 101100
#error Included asio version is too new
#endif

int main()
{
    return 0;
}

"""
    context.Message('Checking ASIO version (>= 1.10.8 and < 1.11.0) ... ')
    result = context.TryLink(system_asio_test_source_file, '.cpp')
    context.Result(result)
    return result

def CheckTr1Array(context):
    test_source = """
#include <tr1/array>
int main() { std::tr1::array<int, 5> a; return 0; }
"""
    context.Message('Checking for std::tr1::array ... ')
    result = context.TryLink(test_source, '.cpp')
    context.Result(result)
    return result

def CheckTr1SharedPtr(context):
    test_source = """
#include <tr1/memory>
int main() { int n; std::tr1::shared_ptr<int> p(&n); return 0; }
"""
    context.Message('Checking for std::tr1::shared_ptr ... ')
    result = context.TryLink(test_source, '.cpp')
    context.Result(result)
    return result

def CheckTr1UnorderedMap(context):
    test_source = """
#include <tr1/unordered_map>
int main() { std::tr1::unordered_map<int, int> m; return 0; }
"""
    context.Message('Checking for std::tr1::unordered_map ... ')
    result = context.TryLink(test_source, '.cpp')
    context.Result(result)
    return result

def CheckWeffcpp(context):
    # Some compilers (gcc <= 4.8 at least) produce a bogus warning for the code
    # below when -Weffc++ is used.
    test_source = """
class A {};
class B : public A {};
int main() { return 0; }
"""
    context.Message('Checking whether to enable -Weffc++ ... ')
    cxxflags_orig = context.env['CXXFLAGS']
    context.env.Prepend(CXXFLAGS = '-Weffc++ -Werror ')
    result = context.TryLink(test_source, '.cpp')
    context.env.Replace(CXXFLAGS = cxxflags_orig)
    context.Result(result)
    return result

# advanced SSL features
def CheckSetEcdhAuto(context):
    test_source = """
#include <openssl/ssl.h>
int main() { SSL_CTX* ctx=NULL; return !SSL_CTX_set_ecdh_auto(ctx, 1); }
"""
    context.Message('Checking for SSL_CTX_set_ecdh_auto() ... ')
    result = context.TryLink(test_source, '.cpp')
    context.Result(result)
    return result

def CheckSetTmpEcdh(context):
    test_source = """
#include <openssl/ssl.h>
int main() { SSL_CTX* ctx=NULL; EC_KEY* ecdh=NULL; return !SSL_CTX_set_tmp_ecdh(ctx,ecdh); }
"""
    context.Message('Checking for SSL_CTX_set_tmp_ecdh_() ... ')
    result = context.TryLink(test_source, '.cpp')
    context.Result(result)
    return result

def CheckVersionScript(context):
    test_source = """
int main() { return 0; }
"""
    context.Message('Checking for --version-script linker option ... ')
    result = context.TryLink(test_source, '.cpp')
    context.Result(result)
    return result

#
# Construct configuration context
#
conf = Configure(env, custom_tests = {
    'CheckCpp11': CheckCpp11,
    'CheckSystemASIOVersion': CheckSystemASIOVersion,
    'CheckTr1Array': CheckTr1Array,
    'CheckTr1SharedPtr': CheckTr1SharedPtr,
    'CheckTr1UnorderedMap': CheckTr1UnorderedMap,
    'CheckWeffcpp': CheckWeffcpp,
    'CheckSetEcdhAuto': CheckSetEcdhAuto,
    'CheckSetTmpEcdh': CheckSetTmpEcdh
})

conf.env.Append(CPPPATH = [ '#/wsrep/src' ])

# System headers and libraries

if not conf.CheckLib('pthread'):
    print('Error: pthread library not found')
    Exit(1)

# libatomic may be needed on some 32bit platforms (and 32bit userland PPC64)
# for 8 byte atomics but not generally required
if not x86:
    conf.CheckLib('atomic')

if sysname != 'darwin':
    if not conf.CheckLib('rt'):
        print('Error: rt library not found')
        Exit(1)

if sysname == 'freebsd':
    if not conf.CheckLib('execinfo'):
        print('Error: execinfo library not found')
        Exit(1)

if sysname == 'sunos':
    if not conf.CheckLib('socket'):
        print('Error: socket library not found')
        Exit(1)
    if not conf.CheckLib('crypto'):
        print('Error: crypto library not found')
        Exit(1)
    if not conf.CheckLib('nsl'):
        print('Error: nsl library not found')
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
    print('can\'t find byte order information')
    Exit(1)

if conf.CheckHeader('execinfo.h'):
    conf.env.Append(CPPFLAGS = ' -DHAVE_EXECINFO_H')

# Additional C headers and libraries

cpp11 = conf.CheckCpp11()

# array
if cpp11:
    conf.env.Append(CPPFLAGS = ' -DHAVE_STD_ARRAY')
elif conf.CheckTr1Array():
    conf.env.Append(CPPFLAGS = ' -DHAVE_TR1_ARRAY')
elif conf.CheckCXXHeader('boost/array.hpp'):
    conf.env.Append(CPPFLAGS = ' -DHAVE_BOOST_ARRAY_HPP')
else:
    print('no suitable array header found')
    Exit(1)

# shared_ptr
if cpp11:
    conf.env.Append(CPPFLAGS = ' -DHAVE_STD_SHARED_PTR')
elif False and conf.CheckTr1SharedPtr():
    # std::tr1::shared_ptr<> is not derived from std::auto_ptr<>
    # this upsets boost in asio, so don't use tr1 version, use boost instead
    conf.env.Append(CPPFLAGS = ' -DHAVE_TR1_SHARED_PTR')
elif conf.CheckCXXHeader('boost/shared_ptr.hpp'):
    conf.env.Append(CPPFLAGS = ' -DHAVE_BOOST_SHARED_PTR_HPP')
else:
    print('no suitable shared_ptr header found')
    Exit(1)

# unordered_map
if cpp11:
    conf.env.Append(CPPFLAGS = ' -DHAVE_STD_UNORDERED_MAP')
elif conf.CheckTr1UnorderedMap():
    conf.env.Append(CPPFLAGS = ' -DHAVE_TR1_UNORDERED_MAP')
elif conf.CheckCXXHeader('boost/unordered_map.hpp'):
    conf.env.Append(CPPFLAGS = ' -DHAVE_BOOST_UNORDERED_MAP_HPP')
else:
    print('no suitable unordered map header found')
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
            print("Error: file '%s' does not exist" % configuredLibPath)
            Exit(1)
        if configuredLibPath == '':
           for libpath in boost_libpaths:
               libname = libpath + '/lib%s.a' % libName
               if os.path.isfile(libname):
                   configuredLibPath = libname
                   break
        if configuredLibPath != '':
            if not conf.CheckCXXHeader(header):
                print("Error: header '%s' does not exist" % header)
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
                print('Error: library %s does not exist' % libName)
                Exit (1)
            return [libName]

    # Required boost headers/libraries
    #
    if boost_pool == 1:
        if conf.CheckCXXHeader('boost/pool/pool_alloc.hpp'):
            print('Using boost pool alloc')
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
            print('Error: boost/pool/pool_alloc.hpp not found or not usable')
            Exit(1)

    libboost_program_options = check_boost_library('boost_program_options',
                                                   'boost/program_options.hpp',
                                                   LIBBOOST_PROGRAM_OPTIONS_A,
                                                   autoadd = 0)
else:
    print('Not using boost')

# asio
if system_asio == 1 and conf.CheckCXXHeader('asio.hpp') and conf.CheckSystemASIOVersion():
    conf.env.Append(CPPFLAGS = ' -DHAVE_ASIO_HPP')
else:
    system_asio = False
    print("Falling back to bundled asio")

if not system_asio:
    # Make sure that -Iasio goes before other paths (e.g. -I/usr/local/include)
    # that may contain a system wide installed asio. We should use the bundled
    # asio if "scons system_asio=0" is specified. Thus use Prepend().
    conf.env.Prepend(CPPPATH = [ '#/asio' ])
    if conf.CheckCXXHeader('asio.hpp'):
        conf.env.Append(CPPFLAGS = ' -DHAVE_ASIO_HPP')
    else:
        print('asio headers not found or not usable')
        Exit(1)

# asio/ssl
if not conf.CheckCXXHeader('asio/ssl.hpp'):
    print('SSL support required but asio/ssl.hpp was not found or not usable')
    print('check that SSL devel headers are installed and usable')
    Exit(1)

def check_static_lib(path, libname):
    fqfilename = path + "/lib" + libname + '.a'
    if os.path.isfile(fqfilename):
        conf.env.Append(LIBS = File(fqfilename))
        return True
    return False

if static_ssl:
    if not check_static_lib(static_ssl, "ssl"):
        print("Static SSL linkage requested but ssl libary not found from {}"
              .format(static_ssl))
        Exit(1)
    if not check_static_lib(static_ssl, "crypto"):
        print("Static SSL requested but crypto libary not found from {}"
              .format(static_ssl))
        Exit(1)
    conf.CheckLib('pthread')
    conf.CheckLib('dl')
    conf.env.Append(LDFLAGS = ' -static-libgcc')
else:
    if not conf.CheckLib('ssl'):
        print('SSL support required but libssl was not found')
        Exit(1)
    if not conf.CheckLib('crypto'):
        print('SSL support required libcrypto was not found')
        Exit(1)

# advanced SSL features
if conf.CheckSetEcdhAuto():
    conf.env.Append(CPPFLAGS = ' -DOPENSSL_HAS_SET_ECDH_AUTO')
elif conf.CheckSetTmpEcdh():
    conf.env.Append(CPPFLAGS = ' -DOPENSSL_HAS_SET_TMP_ECDH')

# these will be used only with our software
if strict_build_flags == 1:
    conf.env.Append(CCFLAGS = ' -Werror ')
    if 'clang' in cxx_version:
        conf.env.Append(CCFLAGS  = ' -Wno-self-assign')
        conf.env.Append(CCFLAGS  = ' -Wno-gnu-zero-variadic-macro-arguments')
        conf.env.Append(CXXFLAGS = ' -Wno-variadic-macros')
        # CXX may be something like "ccache clang++"
        if 'ccache' in conf.env['CXX'] or 'ccache' in conf.env['CC']:
            conf.env.Append(CCFLAGS = ' -Qunused-arguments')
# Enable libstdc++ assertions in debug build.
if int(debug) >= 0:
    conf.env.Append(CXXFLAGS = " -D_GLIBCXX_ASSERTIONS")

if conf.CheckWeffcpp():
    conf.env.Prepend(CXXFLAGS = '-Weffc++ ')

if not 'clang' in cxx_version:
    conf.env.Prepend(CXXFLAGS = '-Wold-style-cast ')

env = conf.Finish()

print('Global flags:')
for f in ['CFLAGS', 'CXXFLAGS', 'CCFLAGS', 'CPPFLAGS']:
    print(f + ': ' + env[f].strip())

Export('machine',
       'x86',
       'bits',
       'env',
       'sysname',
       'libboost_program_options',
       'install')

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

if not x86:
    # don't attempt to run the legacy protocol tests that use unaligned memory
    # access on platforms that are not known to handle it well.
    check_env.Append(CPPFLAGS = ' -DGALERA_ONLY_ALIGNED')

conf = Configure(check_env)

# Check header and library

if not conf.CheckHeader('check.h'):
    print('Error: check header file not found or not usable')
    Exit(1)

if not conf.CheckLib('check'):
    print('Error: check library not found or not usable')
    Exit(1)

if not conf.CheckLib('m'):
    print('Error: math library not found or not usable')
    Exit(1)

# potential check dependency, link if present
conf.CheckLib('subunit')

if sysname != 'darwin':
    if not conf.CheckLib('rt'):
        print('Error: realtime library not found or not usable')
        Exit(1)

conf.Finish()

#
# Check version script linker option
#

test_env = env.Clone()
# Append version script flags to general link options for test
test_env.Append(LINKFLAGS = ' -Wl,--version-script=' + galera_script)

conf = Configure(test_env, custom_tests = {
    'CheckVersionScript': CheckVersionScript,
})

if version_script:
    has_version_script = conf.CheckVersionScript()
else:
    has_version_script = False
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
Export('has_version_script galera_script')

#
# If deterministic_tests is given, export GALERA_TEST_DETERMINISTIC
# so that the non-deterministic tests can be filtered out.
#
if deterministic_tests:
   os.environ['GALERA_TEST_DETERMINISTIC'] = '1'
Export('deterministic_tests all_tests')
#
# Run root SConscript with variant_dir
#
SConscript('SConscript', variant_dir=build_dir)
