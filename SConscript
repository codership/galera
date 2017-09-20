SConscript(['galerautils/SConscript',
            'gcache/SConscript',
            'gcomm/SConscript',
            'gcs/SConscript',
            'galera/SConscript',
            'garb/SConscript'])

Import('env', 'sysname', 'static_ssl', 'with_ssl')

libmmgalera_objs = env['LIBGALERA_OBJS']
libmmgalera_objs.extend(env['LIBMMGALERA_OBJS'])

if static_ssl == 1:
    env.Append(LIBS=File('%s/libssl.a' %(with_ssl)))
    env.Append(LIBS=File('%s/libcrypto.a' %(with_ssl)))
    env.Append(LIBS=File('%s/libz.a' %(with_ssl)))

if sysname == 'darwin':
    env.SharedLibrary('galera_smm', libmmgalera_objs, SHLIBSUFFIX='.so')
else:
    env.SharedLibrary('galera_smm', libmmgalera_objs)
