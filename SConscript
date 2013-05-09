SConscript(['galerautils/SConscript',
            'gcache/SConscript',
            'gcomm/SConscript',
            'gcs/SConscript',
            'galera/SConscript',
            'garb/SConscript'])

Import('env')

libmmgalera_objs = env['LIBGALERA_OBJS']
libmmgalera_objs.extend(env['LIBMMGALERA_OBJS'])

env.SharedLibrary('galera_smm', libmmgalera_objs)
