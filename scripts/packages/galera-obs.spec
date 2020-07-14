# Copyright (c) 2011-2015, Codership Oy <info@codership.com>.
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

%{!?name: %define name galera-3}
%{!?wsrep_api: %define wsrep_api 25}
%{!?version: %define version %{wsrep_api}_3.x}
%{!?release: %define release 2}
%define revision XXXX
%define copyright Copyright 2007-2015 Codership Oy. All rights reserved. Use is subject to license terms under GPLv2 license.
%define libs %{_libdir}/%{name}
%define docs /usr/share/doc/%{name}

# Avoid debuginfo RPMs, leaves binaries unstripped

%global _enable_debug_package 0
%global debug_package %{nil}
%global __os_install_post /usr/lib/rpm/brp-compress %{nil}
%define ssl_package_devel openssl-devel
# Define dist tag if not given by platform

# For suse versions see:
# https://en.opensuse.org/openSUSE:Build_Service_cross_distribution_howto
%if 0%{?suse_version} == 1110
%define dist .sle11
%endif
%if 0%{?suse_version} == 1310
%define dist .suse13.1
%endif
%if 0%{?suse_version} == 1315
%define dist .sle12
%endif
%if 0%{?suse_version} == 1320
%define dist .suse13.2
%endif
%if 0%{?sle_version} == 150000 && 0%{?is_opensuse}
%define dist .lp151
%define ssl_package_devel libopenssl-devel
%endif


Name:          %{name}
Summary:       Galera: a synchronous multi-master wsrep provider (replication engine)
Group:         System Environment/Libraries
Version:       %{version}
Release:       %{release}%{dist}
License:       GPL-2.0
Source:        %{name}-%{version}.tar.gz
URL:           http://www.codership.com/
Packager:      Codership Oy
Vendor:        Codership Oy

BuildRoot:     %{_tmppath}/%{name}_%{version}-build

BuildRequires: boost-devel >= 1.41
BuildRequires: check-devel
BuildRequires: glibc-devel
BuildRequires: %{ssl_package_devel}
%if 0%{?rhel} >= 8 || 0%{?centos} >= 8
BuildRequires: python3-scons
%define scons_cmd scons-3
%else
BuildRequires: scons
%define scons_cmd scons
%endif
%if 0%{?suse_version} == 1110
# On SLES11 SPx use the linked gcc47 to build instead of default gcc43
BuildRequires: gcc47 gcc47-c++
# On SLES11 SP2 the libgfortran.3.so provider must be explicitly defined
BuildRequires: libgfortran3
# On SLES11 we got error "conflict for provider of libgcc_s1 >= 4.7.4_20140612-2.1
# needed by gcc47, (provider libgcc_s1 conflicts with installed libgcc43),
# conflict for provider of libgomp1 >= 4.7.4_20140612-2.1 needed by gcc47,
# (provider libgomp1 conflicts with installed libgomp43), conflict for provider
# of libstdc++6 >= 4.7.4_20140612-2.1 needed by libstdc++47-devel,
# (provider libstdc++6 conflicts with installed libstdc++43)
# therefore:
BuildRequires: libgcc_s1
BuildRequires: libgomp1
BuildRequires: libstdc++6
#!BuildIgnore: libgcc43
%else
BuildRequires: gcc-c++
%endif

%if %{defined fedora}
BuildRequires: python
%endif

# Systemd
%if 0%{?suse_version} >= 1220 || 0%{?centos} >= 7 || 0%{?rhel} >= 7
%define systemd 1
BuildRequires: systemd
%else
%define systemd 0
%endif

%if 0%{?systemd}
%{?systemd_requires}
%if 0%{?suse_version}
BuildRequires: systemd-rpm-macros
# RedHat seems not to need this (or an equivalent).
%endif

%else
# NOT systemd

%if 0%{?suse_version}
PreReq:        %insserv_prereq %fillup_prereq
%else
Requires(post): chkconfig
Requires(preun): chkconfig
Requires(preun): initscripts
%endif
%endif # systemd

Requires:      openssl

Provides:      wsrep, %{name} = %{version}-%{release}
Provides:      galera, galera3, Percona-XtraDB-Cluster-galera-25

%description
Galera is a fast synchronous multimaster wsrep provider (replication engine)
for transactional databases and similar applications. For more information
about wsrep API see http://launchpad.net/wsrep. For a description of Galera
replication engine see http://www.codership.com.

%{copyright}

This software comes with ABSOLUTELY NO WARRANTY. This is free software,
and you are welcome to modify and redistribute it under the GPLv2 license.

%prep
%setup -q -n %{name}-%{version}
# When downloading from GitHub the contents is in a folder
# that is named by the branch it was exported from.

%build
# Debug info:
echo "suse_version: %{suse_version}"
# 1110 = SLE-11 SPx
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

NUM_JOBS=${NUM_JOBS:-$(ncpu=$(cat /proc/cpuinfo | grep processor | wc -l) && echo $(($ncpu > 4 ? 4 : $ncpu)))}

%{scons_cmd} -j$(echo $NUM_JOBS) revno=%{revision} deterministic_tests=1

%install
RBR=$RPM_BUILD_ROOT # eg. rpmbuild/BUILDROOT/galera-3-3.x-33.1.x86_64
RBD=$RPM_BUILD_DIR/%{name}-%{version} # eg. rpmbuild/BUILD/galera-3.x
# When downloading from GitHub the contents is in a folder
# that is named by the branch it was exported from.

# Clean up the BuildRoot first
[ "$RBR" != "/" ] && [ -d $RBR ] && rm -rf $RBR;
mkdir -p $RBR

%if 0%{?systemd}
install -D -m 644 $RBD/garb/files/garb.service $RBR%{_unitdir}/garb.service
install -D -m 755 $RBD/garb/files/garb-systemd $RBR%{_bindir}/garb-systemd
%else
install -d $RBR%{_sysconfdir}/init.d
install -m 755 $RBD/garb/files/garb.sh  $RBR%{_sysconfdir}/init.d/garb
%endif

# Symlink required by SUSE policy for SysV init, still supported with systemd
%if 0%{?suse_version}
%if 0%{?systemd}
install -d %{buildroot}%{_sbindir}
ln -sf /usr/sbin/service %{buildroot}%{_sbindir}/rcgarb
%else
install -d $RBR/usr/sbin
ln -sf /etc/init.d/garb $RBR/usr/sbin/rcgarb
%endif # systemd
%endif # suse_version

%if 0%{?suse_version}
install -d $RBR/var/adm/fillup-templates/
install -m 644 $RBD/garb/files/garb.cnf $RBR/var/adm/fillup-templates/sysconfig.garb
%else
install -d $RBR%{_sysconfdir}/sysconfig
install -m 644 $RBD/garb/files/garb.cnf $RBR%{_sysconfdir}/sysconfig/garb
%endif # suse_version

install -d $RBR%{_bindir}
install -m 755 $RBD/garb/garbd                    $RBR%{_bindir}/garbd

install -d $RBR%{libs}
install -m 755 $RBD/libgalera_smm.so              $RBR%{libs}/libgalera_smm.so

install -d $RBR%{docs}
install -m 644 $RBD/COPYING                       $RBR%{docs}/COPYING
install -m 644 $RBD/asio/LICENSE_1_0.txt          $RBR%{docs}/LICENSE.asio
install -m 644 $RBD/chromium/LICENSE              $RBR%{docs}/LICENSE.chromium
install -m 644 $RBD/scripts/packages/README       $RBR%{docs}/README
install -m 644 $RBD/scripts/packages/README-MySQL $RBR%{docs}/README-MySQL

install -d $RBR%{_mandir}/man8
install -m 644 $RBD/man/garbd.8        $RBR%{_mandir}/man8/garbd.8


%if 0%{?systemd}

%if 0%{?suse_version}

%post
%service_add_post garb

%preun
%service_del_preun garb

%else
# Not SuSE - so it must be RedHat, CentOS, Fedora

%post
%systemd_post garb.service

%preun
%systemd_preun garb.service

%postun
%systemd_postun_with_restart garb.service

%endif
# SuSE versus Fedora/RedHat/CentOS

%else
# NOT systemd

%if 0%{?suse_version}
# For the various macros and their parameters, see here:
# https://en.opensuse.org/openSUSE:Packaging_Conventions_RPM_Macros

%post
%fillup_and_insserv garb

%preun
%stop_on_removal garb
rm -f $(find %{libs} -type l)

%postun
%restart_on_update garb
%insserv_cleanup

%else
# Not SuSE - so it must be RedHat, CentOS, Fedora

%post
/sbin/chkconfig --add garb

%preun
if [ "$1" = "0" ]
then
    /sbin/service garb stop
    /sbin/chkconfig --del garb
fi

%postun
# >=1 packages after uninstall -> pkg was updated -> restart
if [ "$1" -ge "1" ]
then
    /sbin/service garb restart
fi

%endif
# SuSE versus Fedora/RedHat/CentOS

%endif
# systemd ?


%files
%defattr(-,root,root,0755)
%if 0%{?suse_version}
%config(noreplace,missingok) /var/adm/fillup-templates/sysconfig.garb
%else
%config(noreplace,missingok) %{_sysconfdir}/sysconfig/garb
%endif


%if 0%{?systemd}
%attr(0644,root,root) %{_unitdir}/garb.service
%attr(0755,root,root) %{_bindir}/garb-systemd
%else
%attr(0755,root,root) %{_sysconfdir}/init.d/garb
%endif

# Symlink required by SUSE policy for SysV init, still supported with systemd
%if 0%{?suse_version}
%attr(0755,root,root) /usr/sbin/rcgarb
%endif

%attr(0755,root,root) %{_bindir}/garbd

%attr(0755,root,root) %dir %{libs}
%attr(0755,root,root) %{libs}/libgalera_smm.so

%attr(0755,root,root) %dir %{docs}
%doc %attr(0644,root,root) %{docs}/COPYING
%doc %attr(0644,root,root) %{docs}/LICENSE.asio
%doc %attr(0644,root,root) %{docs}/LICENSE.chromium
%doc %attr(0644,root,root) %{docs}/README
%doc %attr(0644,root,root) %{docs}/README-MySQL

%doc %attr(644, root, man) %{_mandir}/man8/garbd.8*

%clean
[ "$RPM_BUILD_ROOT" != "/" ] && [ -d $RPM_BUILD_ROOT ] && rm -rf $RPM_BUILD_ROOT;

%changelog
* Fri Feb 27 2015 Joerg Bruehe <joerg.bruehe@fromdual.com>
- Service name is "garb", reflect that in the config file (SuSE only, galera#235, Release 2)

* Fri Feb 20 2015 Joerg Bruehe <joerg.bruehe@fromdual.com>
- Update copyright year.
- Make the man page file name consistent with its section.

* Wed Feb 11 2015 Joerg Bruehe <joerg.bruehe@fromdual.com>
- Add missing "prereq" directive and arguments for the various service control macros.
- Handle the difference between SuSE and Fedora/RedHat/CentOS.
- Fix systemd stuff, using info from these pages:
  https://en.opensuse.org/openSUSE:Systemd_packaging_guidelines
  http://fedoraproject.org/wiki/Packaging:Systemd
  http://fedoraproject.org/wiki/Packaging:ScriptletSnippets#Systemd

* Tue Sep 30 2014 Otto Kekäläinen <otto@seravo.fi> - 3.x
- Initial OBS packaging created

