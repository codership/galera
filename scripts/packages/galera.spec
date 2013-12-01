# Copyright (c) 2011, Codership Oy <info@codership.com>. All rights reserved.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; version 2 of the License or later.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; see the file COPYING. If not, write to the
# Free Software Foundation, Inc., 51 Franklin St, Fifth Floor, Boston
# MA  02110-1301  USA.

%define name galera
# use "rpmbuild --define 'version xxxx'" to define version
%{!?version: %define version 3.x}
%{!?release: %define release 1}
%define copyright Copyright 2007-2013 Codership Oy. All rights reserved. Use is subject to license terms under GPLv2 license.
%define libs %{_libdir}/%{name}
%define docs /usr/share/doc/%{name}

Name:          %{name}
Summary:       Galera: a synchronous multi-master wsrep provider (replication engine)
Group:         System Environment/Libraries
Version:       %{version}
Release:       %{release}
License:       GPLv2
Source:        http://www.codership.com/downloads/download-mysqlgalera/
URL:           http://www.codership.com/
Packager:      Codership Oy
Vendor:        Codership Oy
Provides:      %{name} wsrep
Obsoletes:     %{name}
Requires:      chkconfig
#Requires:      boost-program-options
# BuildRequires: scons check boost-devel
# This will be rm -rf
BuildRoot:     %{_tmppath}/%{name}-%{version}

%description
Galera is a fast synchronous multimaster wsrep provider (replication engine)
for transactional databases and similar applications. For more information
about wsrep API see http://launchpad.net/wsrep. For a description of Galera
replication engine see http://www.codership.com.

%{copyright}

This software comes with ABSOLUTELY NO WARRANTY. This is free software,
and you are welcome to modify and redistribute it under the GPLv2 license.

%prep
#%setup -T -a 0 -c -n galera-%{version}

%build
Build() {
CFLAGS=${CFLAGS:-$RPM_OPT_FLAGS}
CXXFLAGS=${CXXFLAGS:-$RPM_OPT_FLAGS}
# We assume that Galera is built already by the top build.sh script
}

%install
RBR=$RPM_BUILD_ROOT
RBD=$RPM_BUILD_DIR

# Clean up the BuildRoot first
[ "$RBR" != "/" ] && [ -d $RBR ] && rm -rf $RBR;
mkdir -p $RBR

install -d $RBR%{_sysconfdir}/{init.d,sysconfig}
install -m 644 $RBD/garb/files/garb.cnf $RBR%{_sysconfdir}/sysconfig/garb
install -m 755 $RBD/garb/files/garb.sh  $RBR%{_sysconfdir}/init.d/garb

install -d $RBR%{_bindir}
install -m 755 $RBD/garb/garbd                    $RBR%{_bindir}/garbd

install -d $RBR%{libs}
install -m 755 $RBD/libgalera_smm.so              $RBR%{libs}/libgalera_smm.so

install -d $RBR%{docs}
install -m 644 $RBD/COPYING                       $RBR%{docs}/COPYING
install -m 644 $RBD/asio/LICENSE_1_0.txt          $RBR%{docs}/LICENSE.asio
install -m 644 $RBD/www.evanjones.ca/LICENSE      $RBR%{docs}/LICENSE.crc32c
install -m 644 $RBD/chromium/LICENSE              $RBR%{docs}/LICENSE.chromium
install -m 644 $RBD/scripts/packages/README       $RBR%{docs}/README
install -m 644 $RBD/scripts/packages/README-MySQL $RBR%{docs}/README-MySQL

%pre

%post

%preun
rm -f $(find %{libs} -type l)

%files
%defattr(-,root,root,0755)
%config(noreplace,missingok) %{_sysconfdir}/sysconfig/garb
%attr(0755,root,root) %{_sysconfdir}/init.d/garb

%attr(0755,root,root) %dir %{_bindir}
%attr(0755,root,root) %{_bindir}/garbd

%attr(0755,root,root) %dir %{libs}
%attr(0755,root,root) %{libs}/libgalera_smm.so

%attr(0755,root,root) %dir %{docs}
%doc %attr(0644,root,root) %{docs}/COPYING
%doc %attr(0644,root,root) %{docs}/LICENSE.asio
%doc %attr(0644,root,root) %{docs}/LICENSE.crc32c
%doc %attr(0644,root,root) %{docs}/LICENSE.chromium
%doc %attr(0644,root,root) %{docs}/README
%doc %attr(0644,root,root) %{docs}/README-MySQL

%clean
[ "$RPM_BUILD_ROOT" != "/" ] && [ -d $RPM_BUILD_ROOT ] && rm -rf $RPM_BUILD_ROOT;

