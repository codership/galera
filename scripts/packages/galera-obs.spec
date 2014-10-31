# Copyright (c) 2011-2014, Codership Oy <info@codership.com>.
# All rights reserved.
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
%{!?version: %define version 3.x}
%{!?release: %define release 1}
%define copyright Copyright 2007-2014 Codership Oy. All rights reserved. Use is subject to license terms under GPLv2 license.
%define libs %{_libdir}/%{name}
%define docs /usr/share/doc/%{name}

Name:          %{name}
Summary:       Galera: a synchronous multi-master wsrep provider (replication engine)
Group:         System Environment/Libraries
Version:       %{version}
Release:       %{release}
License:       GPL-2.0
Source:        %{name}-%{version}.tar.gz
URL:           http://www.codership.com/
Packager:      Codership Oy
Vendor:        Codership Oy

BuildRoot:     %{_tmppath}/%{name}-%{version}-build

BuildRequires: boost-devel
BuildRequires: check-devel
BuildRequires: glibc-devel
BuildRequires: openssl-devel
BuildRequires: scons
%if 0%{?suse_version} == 1110
# On SLES11 SPx use the linked gcc47 to build instead of default gcc43
BuildRequires: gcc47-c++
%else
BuildRequires: gcc-c++
%endif

%if %{defined fedora}
BuildRequires: python
%endif

Requires:      libssl0.9.8
Requires:      chkconfig

Provides:      wsrep, %{name} = %{version}-%{release}
Obsoletes:     %{name} < %{version}-%{release}

%description
Galera is a fast synchronous multimaster wsrep provider (replication engine)
for transactional databases and similar applications. For more information
about wsrep API see http://launchpad.net/wsrep. For a description of Galera
replication engine see http://www.codership.com.

%{copyright}

This software comes with ABSOLUTELY NO WARRANTY. This is free software,
and you are welcome to modify and redistribute it under the GPLv2 license.

%prep
%setup -q

%build
# Debug info:
echo "suse_version: %{suse_version}"
%if 0%{?suse_version} == 1110
export CC=gcc-4.7
export CXX=g++-4.7
%endif
%if 0%{?suse_version} == 1120
export CC=gcc-4.6
export CXX=g++-4.6
%endif
%if 0%{?suse_version} == 1130
export CC=gcc-4.7
export CXX=g++-4.7
%endif

scons -j$(echo ${NUM_JOBS:-"1"})

%install
RBR=$RPM_BUILD_ROOT # eg. rpmbuild/BUILDROOT/galera-3.x-17.1.x86_64
RBD=$RPM_BUILD_DIR/%{name}-%{version} # eg. rpmbuild/BUILD/galera-3.x

# Clean up the BuildRoot first
[ "$RBR" != "/" ] && [ -d $RBR ] && rm -rf $RBR;
mkdir -p $RBR

install -d $RBR%{_sysconfdir}/init.d
install -m 755 $RBD/garb/files/garb.sh  $RBR%{_sysconfdir}/init.d/garb

%if 0%{?suse_version}
install -d $RBR/var/adm/fillup-templates/
install -m 644 $RBD/garb/files/garb.cnf $RBR/var/adm/fillup-templates/sysconfig.%{name}
%else
install -d $RBR%{_sysconfdir}/sysconfig
install -m 644 $RBD/garb/files/garb.cnf $RBR%{_sysconfdir}/sysconfig/garb
%endif

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

install -d $RBR%{_mandir}/man1
install -m 644 $RBD/garb/files/garbd.troff        $RBR%{_mandir}/man1/garbd.1

%post
%fillup_and_insserv

%preun
%stop_on_removal
rm -f $(find %{libs} -type l)

%postun
%restart_on_update

%files
%defattr(-,root,root,0755)
%if 0%{?suse_version}
%config(noreplace,missingok) /var/adm/fillup-templates/sysconfig.%{name}
%else
%config(noreplace,missingok) %{_sysconfdir}/sysconfig/garb
%endif
%attr(0755,root,root) %{_sysconfdir}/init.d/garb

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

%doc %attr(644, root, man) %{_mandir}/man1/garbd.1*

%clean
[ "$RPM_BUILD_ROOT" != "/" ] && [ -d $RPM_BUILD_ROOT ] && rm -rf $RPM_BUILD_ROOT;

%changelog
* Tue Sep 30 2014 Otto Kekäläinen <otto@seravo.fi> - 3.x
- Initial OBS packaging created

