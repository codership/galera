# This is Galera package description for ESP package manager

%product     Galera Replication Framework
%copyright   2007-2010 by Codership Oy, All Rights Reserved
%vendor      Codership Oy
%license     COPYING
%readme      README
%description Replication framework for transactional applications. Implements wsrep interface.
$galera_ver=0.7.3pre
%version     ${galera_ver}
%provides    wsrep
%provides    galera

%requires    libc6      2.4
%requires    libstdc++6 4.1.1
%requires    libgcc1    4.1.1

$prefix=/usr

$LIBS_DEST=${prefix}/lib/galera
$INCS_DEST=${prefix}/include/galera
$SBIN_DEST=${prefix}/sbin
$BINS_DEST=${prefix}/bin

#
