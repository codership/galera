#!/bin/bash -eu

if [ $# -ne 1 ]
then
    echo "Usage: $0 <version>"
    exit 1
fi
RELEASE=$1

# Absolute path of this script folder
SCRIPT_ROOT=$(cd $(dirname $0); pwd -P)

PBR="$SCRIPT_ROOT/pkg_top_dir"
PBD="$SCRIPT_ROOT/../.."
GALERA_LICENSE_DIR="$PBR/share/licenses/galera-$RELEASE"

rm -rf "$PBR"
mkdir -p "$PBR"

install -d "$PBR/"{bin,lib/galera,share/doc/galera,etc/rc.d,libdata/ldconfig}
install -m 555 "$PBD/garb/files/freebsd/garb.sh"      "$PBR/etc/rc.d/garb"
install -m 555 "$PBD/garb/garbd"                      "$PBR/bin/garbd"
install -m 444 "$PBD/libgalera_smm.so"                "$PBR/lib/galera/libgalera_smm.so"
install -m 444 "$SCRIPT_ROOT/freebsd/galera-ldconfig" "$PBR/libdata/ldconfig/galera"
install -m 444 "$PBD/scripts/packages/README"         "$PBR/share/doc/galera/"
install -m 444 "$PBD/scripts/packages/README-MySQL"   "$PBR/share/doc/galera/"

install -m 755 -d "$GALERA_LICENSE_DIR"
install -m 444 "$PBD/LICENSE"                             "$GALERA_LICENSE_DIR/GPLv2"
install -m 444 "$PBD/scripts/packages/freebsd/LICENSE"    "$GALERA_LICENSE_DIR"
install -m 444 "$PBD/asio/LICENSE_1_0.txt"                "$GALERA_LICENSE_DIR/LICENSE.asio"
install -m 444 "$PBD/scripts/packages/freebsd/catalog.mk" "$GALERA_LICENSE_DIR"

install -m 644 "$SCRIPT_ROOT/freebsd/galera-"{plist,descr,comment,message} "$PBR"
sed -e "s!%{SRCDIR}!$PBR!" -e "s!%{RELEASE}!$RELEASE!" -i "" "$PBR/galera-"{plist,descr,comment,message} \
        "$GALERA_LICENSE_DIR/catalog.mk"
for pkg in $(grep '^@comment DEPORIGIN:' "$PBR/galera-plist" | cut -d : -f 2); do
    pkgdep=$(/usr/sbin/pkg_info -q -O "$pkg")
    if [ -z "$pkgdep" ]; then
        echo "ERROR: failed to find dependency package '$pkg'" >&2
        exit 1
    fi
    sed -e "s!^@comment DEPORIGIN:$pkg!@pkgdep $pkgdep"$'\\\n&!' -i "" "$PBR/galera-plist"
done

/usr/sbin/pkg_create -c "$SCRIPT_ROOT/freebsd/galera-comment" \
                     -d "$SCRIPT_ROOT/freebsd/galera-descr" \
                     -m "$SCRIPT_ROOT/freebsd/galera-mtree" \
                     -D "$SCRIPT_ROOT/freebsd/galera-message" \
                     -f "$PBR/galera-plist" \
                     -v "galera-$1-$(uname -m).tbz"

rm -rf "$PBR"

exit 0
