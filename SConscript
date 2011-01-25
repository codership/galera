SConscript(['galerautils/SConscript',
            'gcache/SConscript',
            'gcomm/SConscript',
            'gcs/SConscript',
            'galera/SConscript'])


env = DefaultEnvironment()

libmmgalera_objs = env['LIBGALERA_OBJS']
libmmgalera_objs.extend(env['LIBMMGALERA_OBJS'])

env.SharedLibrary('mmgalera', libmmgalera_objs)

