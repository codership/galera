#!/bin/bash -eu

if [ $# -ne 2 ]
then
    echo "Usage: $0 <mysql_version> <wsrep_version>"
    exit 1
fi

MYSQL_VER=$1
RELEASE=$2
MAJORMINOR=$(echo $MYSQL_VER | awk -F . '{print $1$2}')

# Absolute path of this script folder
SCRIPT_ROOT=$(cd $(dirname $0); pwd -P)

PBR="$SCRIPT_ROOT/dist/mysql/usr/local"

install -d "$PBR"/{etc/rc.d,libdata/ldconfig}
install -m 555 "$SCRIPT_ROOT/freebsd/mysql-server.sh" "$PBR/etc/rc.d/mysql-server"
install -m 444 "$SCRIPT_ROOT/freebsd/client-ldconfig" "$PBR/libdata/ldconfig/mysql${MAJORMINOR}-client"
shopt -s nullglob
for i in {1..9}; do
    for f in "$PBR/man/man$i/"*.$i; do
        gzip -c $f > $f.gz
    done
done
shopt -u nullglob

install -m 644 "$SCRIPT_ROOT/freebsd/server-"{plist,descr,comment,message} "$PBR"
sed -e "s!%{SRCDIR}!$PBR!" -e "s!%{RELEASE}!$RELEASE!" -e "s!%{MYSQL_VER}!$MYSQL_VER!" \
        -e "s!%{MAJORMINOR}!$MAJORMINOR!" -i "" "$PBR/server-"{plist,descr,comment,message} \
	"$PBR/share/licenses/mysql-client-${MYSQL_VER}_wsrep_${RELEASE}/catalog.mk"
for pkg in $(grep '^@comment DEPORIGIN:' "$PBR/server-plist" | cut -d : -f 2); do
    if [[ "$pkg" != *_wsrep* ]]; then
        pkgdep=$(/usr/sbin/pkg_info -q -O "$pkg")
        if [ -z "$pkgdep" ]; then
            echo "ERROR: failed to find dependency package '$pkg'" >&2
            exit 1
        fi
        sed -e "s!^@comment DEPORIGIN:$pkg!@pkgdep $pkgdep"$'\\\n&!' -i "" "$PBR/server-plist"
    fi
done

/usr/sbin/pkg_create -c "$PBR/server-comment" \
                     -d "$PBR/server-descr" \
                     -D "$PBR/server-message" \
                     -f "$PBR/server-plist" \
                     -m "$SCRIPT_ROOT/freebsd/server-mtree" \
                     -v "mysql-server-${MYSQL_VER}_wsrep_${RELEASE}-$(uname -m).tbz"

install -m 644 "$SCRIPT_ROOT/freebsd/client-"{plist,descr,comment,message} "$PBR/"
sed -e "s!%{SRCDIR}!$PBR!" -e "s!%{RELEASE}!$RELEASE!" -e "s!%{MYSQL_VER}!$MYSQL_VER!" \
        -e "s!%{MAJORMINOR}!$MAJORMINOR!" -i "" "$PBR/client-"{plist,descr,comment,message} \
	"$PBR/share/licenses/mysql-client-${MYSQL_VER}_wsrep_${RELEASE}/catalog.mk"
for pkg in $(grep '^@comment DEPORIGIN:' "$PBR/client-plist" | cut -d : -f 2); do
    if [[ "$pkg" != *_wsrep* ]]; then
        pkgdep=$(/usr/sbin/pkg_info -q -O "$pkg")
        if [ -z "$pkgdep" ]; then
            echo "ERROR: failed to find dependency package '$pkg'" >&2
            exit 1
        fi
        sed -e "s!^@comment DEPORIGIN:$pkg!@pkgdep $pkgdep"$'\\\n&!' -i "" "$PBR/client-plist"
    fi
done

/usr/sbin/pkg_create -c "$PBR/client-comment" \
                     -d "$PBR/client-descr" \
                     -D "$PBR/client-message" \
                     -f "$PBR/client-plist" \
                     -m "$SCRIPT_ROOT/freebsd/client-mtree" \
                     -v "mysql-client-${MYSQL_VER}_wsrep_${RELEASE}-$(uname -m).tbz"

exit 0
