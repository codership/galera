#
# SCons build script to build galera libraries
#
# Commandline args:
#
# debug=n, debug build with optimization level n
# arch=str, target architecture [i386|x86-64]
# build_dir=dir, build directory, defaults to ./
#

#
# Default params
#

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
        

#
# Set up and export build environment
#
# TODO: import env required for ccache and distcc 
env = Environment()

#
# Check required headers and libraries
#

conf = Configure(env)
    
if not conf.CheckHeader('check.h'):
    print 'check.h not found'
    Exit(1)

# Check
# This seems to append libcheck into linker flags unconditionally, 
# perhaps the check should be done for each target separately?
# if not conf.CheckLib('check'):
#     print 'Did not find libcheck'
#    Exit(1)


# Required boost headers/libraries
if not conf.CheckCXXHeader('boost/pool/pool_alloc.hpp'):
    print 'boost/pool/pool_alloc.hpp not found'
    Exit(1)

env = conf.Finish()

#
# Set up build flags
# 

# Include paths
env.Replace(CPPPATH = Split('''#/galerautils/src
                               #/gcomm/src
                               #/gcomm/src/gcomm
                               #/gcs/src
                               #/wsdb/src
                               #/galera/src
                               '''))

# Common C/CXX flags
# These should be kept minimal as they are appended after C/CXX specific flags
env.Replace(CCFLAGS = opt_flags + ' -Wall -Wextra -Werror -Wno-unused-parameter ' + compile_arch)

# Linker flags
env.Append(LINKFLAGS = ' ' + link_arch)

# CPPFLAGS
env.Append(CPPFLAGS = ' -D_XOPEN_SOURCE=600')

# CFLAGS
env.Replace(CFLAGS = '-std=c99 -fno-strict-aliasing -pedantic')

# CXXFLAGS
env.Replace(CXXFLAGS = '-Wno-long-long -Wno-deprecated -Weffc++ -pedantic -ansi')

Export('env')


SConscript('SConscript', variant_dir=build_dir)
