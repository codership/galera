#!/bin/bash -eu

if [ $# -ne 2 ]
then
    echo "Usage: $0 <mysql_version> <wsrep_version>"
    exit 1
fi

MYSQL_VER=$1
VERSION=$2

# Absolute path of this script folder
SCRIPT_ROOT=$(cd $(dirname $0); pwd -P)

PBR="$SCRIPT_ROOT/dist/mysql/usr/local"

install -d "$PBR"/{etc/rc.d,libdata/ldconfig}
install -m 555 "$SCRIPT_ROOT/freebsd/mysql-server.sh" "$PBR/etc/rc.d/mysql-server"
install -m 444 "$SCRIPT_ROOT/freebsd/client-ldconfig" "$PBR/libdata/ldconfig/mysql55-client"
[ -f "$PBR/man/man1/mysqlman.1" ] && gzip "$PBR/man/man1/mysqlman.1"

install -m 644 "$SCRIPT_ROOT/freebsd/server-plist" "$PBR/server-plist"
sed -e "s!%{SRCDIR}!$PBR!" -e "s!%{VERSION}!$2!" -i "" "$PBR/server-plist"
for pkg in $(grep '^@comment DEPORIGIN:' "$PBR/server-plist" | cut -d : -f 2); do
        if [[ "$pkg" != *_wsrep* ]]; then
                pkgdep=$(/usr/sbin/pkg_info -q -O "$pkg")
                sed -e "s!^@comment DEPORIGIN:$pkg!@pkgdep $pkgdep"$'\\\n&!' -i "" "$PBR/server-plist"
        fi
done

/usr/sbin/pkg_create -c "$SCRIPT_ROOT/freebsd/server-comment" \
                     -d "$SCRIPT_ROOT/freebsd/server-descr" \
                     -m "$SCRIPT_ROOT/freebsd/server-mtree" \
                     -D "$SCRIPT_ROOT/freebsd/server-message" \
                     -f "$PBR/server-plist" \
                     -v "mysql-server-$1_wsrep_$2-$(uname -m).tbz"

install -m 644 "$SCRIPT_ROOT/freebsd/client-plist" "$PBR/client-plist"
sed -e "s!%{SRCDIR}!$PBR!" -e "s!%{VERSION}!$2!" -i "" "$PBR/client-plist"
for pkg in $(grep '^@comment DEPORIGIN:' "$PBR/client-plist" | cut -d : -f 2); do
        if [[ "$pkg" != *_wsrep* ]]; then
                pkgdep=$(/usr/sbin/pkg_info -q -O "$pkg")
                sed -e "s!^@comment DEPORIGIN:$pkg!@pkgdep $pkgdep"$'\\\n&!' -i "" "$PBR/client-plist"
        fi
done

/usr/sbin/pkg_create -c "$SCRIPT_ROOT/freebsd/client-comment" \
                     -d "$SCRIPT_ROOT/freebsd/client-descr" \
                     -m "$SCRIPT_ROOT/freebsd/client-mtree" \
                     -D "$SCRIPT_ROOT/freebsd/client-message" \
                     -f "$PBR/client-plist" \
                     -v "mysql-client-$1_wsrep_$2-$(uname -m).tbz"

exit 0
