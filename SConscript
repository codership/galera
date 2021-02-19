SConscript(['galerautils/SConscript',
            'gcache/SConscript',
            'gcomm/SConscript',
            'gcs/SConscript',
            'galera/SConscript',
            'garb/SConscript',
            'wsrep/tests/SConscript'])

Import('env', 'sysname', 'has_version_script', 'galera_script', 'install',
       'have_ssl')

# Clone the environment as it will be extended for this specific library
env = env.Clone()

if has_version_script:
    # Limit symbols visible from Galera DSO.
    # Doing this allows to:
    # - make the ABI more clean and concise
    # - hide symbols from commonly used libraries (boost, asio, etc.), which
    #   binds calls inside the DSO to its own versions of these libraries
    # See: https://akkadia.org/drepper/dsohowto.pdf (section 2.2.5)
    env.Append(SHLINKFLAGS = ' -Wl,--version-script=' + galera_script)

libmmgalera_objs = env['LIBGALERA_OBJS']
libmmgalera_objs.extend(env['LIBMMGALERA_OBJS'])

if sysname == 'darwin':
    galera_lib = env.SharedLibrary('galera_smm', libmmgalera_objs, SHLIBSUFFIX='.so')
else:
    galera_lib = env.SharedLibrary('galera_smm', libmmgalera_objs)

if has_version_script:
    env.Depends(galera_lib, galera_script)


def check_executable_exists(command):
    from subprocess import check_call
    from os import devnull
    DEVNULL = open(devnull, 'w') # when subprocess.DEVNULL is not available
    try:
        check_call(command, stdout=DEVNULL)
        retval = 0
    except:
        print(command[0] + ' is not found.')
        retval = 1
    DEVNULL.close()
    return retval

def check_dynamic_symbols(target, source, env):
    # Check if objdump exists
    if check_executable_exists(['objdump', '--version']):
        print('Skipping dynamic symbols check.')
        return 0

    # Check that DSO doesn't contain asio-related dynamic symbols
    if env.Execute(Action(['! objdump -T ' + target[0].abspath + ' | grep asio'], None)):
        return 1
    return 0

if has_version_script:
    env.AddPostAction(galera_lib, Action(check_dynamic_symbols,
                                         'Checking dynamic symbols for \'$TARGET\'...'))

def check_no_ssl_linkage(target, source, env):
    # Check if ldd exists
    if check_executable_exists(['ldd', '--version']):
        print('ldd utility is not found. Skipping linkage checks...')
        return 0

    if env.Execute(Action(['! ldd ' + target[0].abspath + ' | grep ssl'], None)):
        return 1
    if env.Execute(Action(['! ldd ' + target[0].abspath + ' | grep crypto'], None)):
        return 1

    if check_executable_exists(['nm', '--version']):
        print('nm utility not found, Skipping symbol checks...')
        return 0
    if env.Execute(Action([' ! nm ' + target[0].abspath + ' | grep OPENSSL'],
                          None)):
        return 1

# Verify that no SSL libraries were linked and no SSL symbols can be found
# if SSL was not enabled.
if not have_ssl:
    env.AddPostAction(galera_lib, Action(check_no_ssl_linkage,
                                         'Checking no-SSL linkage for \'$TARGET\'...'))

if install:
    env.Install(install + '/lib', '#libgalera_smm.so')
    env.Install(install + '/bin', '#garb/garbd')
    env.Install(install + '/share',
                '#garb/files/garb.service')
    env.Install(install + '/share',
                '#garb/files/garb-systemd')
    env.Install(install + '/share',
                '#garb/files/garb-systemd')
    env.Install(install + '/share',
                '#garb/files/garb.cnf')
    env.Install(install + '/doc/', '#COPYING')
    env.Install(install + '/doc/', '#AUTHORS')
    env.InstallAs(install + '/doc/LICENSE.asio',
                '#asio/LICENSE_1_0.txt')
    env.InstallAs(install + '/doc/README',
                '#scripts/packages/README')
    env.Install(install + '/man/man8', '#man/garbd.8')
    env.Install(install + '/man/man8', '#man/garb-systemd.8')
