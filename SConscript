SConscript(['galerautils/SConscript',
            'gcache/SConscript',
            'gcomm/SConscript',
            'gcs/SConscript',
            'galera/SConscript',
            'garb/SConscript'])

Import('env', 'sysname')

libmmgalera_objs = env['LIBGALERA_OBJS']
libmmgalera_objs.extend(env['LIBMMGALERA_OBJS'])

if sysname == 'darwin':
    galera_lib = env.SharedLibrary('galera_smm', libmmgalera_objs, SHLIBSUFFIX='.so')
else:
    galera_lib = env.SharedLibrary('galera_smm', libmmgalera_objs)

def check_no_dynamic_dispatch(target, source, env):
    import subprocess

    # Check if objdump exists
    p = subprocess.Popen(['objdump', '--version'], stdout=subprocess.PIPE)
    p.wait()
    if p.returncode != 0:
        print('objdump utility is not found. Skipping checks...')
        return 0

    # Check that PLT doesn't contain boost-related symbols
    if env.Execute(Action(['! objdump -D --section=.plt ' + target[0].abspath + ' | grep boost'], None)):
        return 1
    return 0

env.AddPostAction(galera_lib, Action(check_no_dynamic_dispatch,
                  'Checking dynamic dispatch is off for \'$TARGET\'...'))
