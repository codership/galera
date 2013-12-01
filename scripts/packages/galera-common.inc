# This is Galera package description for ESP package manager

%product     Galera Replication Framework
%copyright   2007-2013 by Codership Oy, All Rights Reserved
%vendor      Codership Oy
%license     COPYING
%readme      README
%description Replication framework for transactional applications. Implements wsrep interface.
%version     ${GALERA_VER}
%provides    wsrep
%provides    galera

%format deb
%replaces    galera
%requires    libc6      2.4
%requires    libstdc++6 4.1.1
%requires    libgcc1    4.1.1
%requires    libssl0.9.8
%format rpm
%requires    glibc      2.4
%requires    libstdc++  4.1.2
%requires    libgcc     4.1.2
%requires    libssl.so.6
%format all

$prefix=/usr

%format !rpm
$CONF_DEST=/etc/default
$LIBS_DEST=${prefix}/lib/galera
%format rpm
$CONF_DEST=/etc/sysconfig
%if x86_64
$LIBS_DEST=${prefix}/lib64/galera
%else
$LIBS_DEST=${prefix}/lib/galera
%endif
%format all
$INIT_DEST=/etc/init.d
$INCS_DEST=${prefix}/include/galera
$SBIN_DEST=${prefix}/sbin
$BINS_DEST=${prefix}/bin
$DOCS_DEST=${prefix}/share/doc/galera
#
