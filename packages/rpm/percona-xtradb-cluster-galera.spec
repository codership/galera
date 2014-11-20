# Copyright (c) 2011,  Percona Inc.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; version 3 of the License.
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

%define src_dir percona-xtradb-cluster-galera-3
%define docs /usr/share/doc/%{src_dir}

%define src_dir2 percona-xtradb-cluster-garbd-3
%define docs2 /usr/share/doc/%{src_dir2}
Prefix: %{_prefix}

%define rhelver %(rpm -qf --qf '%%{version}\\n' /etc/redhat-release | sed -e 's/^\\([0-9]*\\).*/\\1/g')
%if "%rhelver" == "5"
 %define boost_req boost141-devel
 %define gcc_req gcc44-c++
%else
 %define boost_req boost-devel
 %define gcc_req gcc-c++
%endif

%if %{undefined scons_args}
 %define scons_args %{nil}
%endif

%if %{undefined galera_version}
 %define galera_version 3.8
%endif

%if %{undefined galera_revision}
 %define galera_revision %{revision}
%endif

%if %{undefined pxcg_revision}
 %define pxcg_revision %{revno}
%endif

%ifarch i686
 %define scons_arch arch=i686
%else
 %define scons_arch %{nil}
%endif


%bcond_with systemd
#
%if %{with systemd}
  %define systemd 1
%else
  %if 0%{?rhel} > 6
    %define systemd 1
  %else
    %define systemd 0
  %endif
%endif

%define redhatversion %(lsb_release -rs | awk -F. '{ print $1}')
%define distribution  rhel%{redhatversion}

%if "%rhel" == "7"
    %define distro_requires           chkconfig nmap
%else
    %define distro_requires           chkconfig nc
%endif


Name:		Percona-XtraDB-Cluster-galera-3
Version:	%{galera_version}
Release:	1.%{pxcg_revision}.%{?distribution}
Summary:	Galera libraries of Percona XtraDB Cluster
Group:		Applications/Databases
License:	GPLv3
URL:		http://www.percona.com/
Source0:        percona-xtradb-cluster-galera-3.tar.gz
BuildRoot:	%(mktemp -ud %{_tmppath}/%{name}-%{version}-%{release}-XXXXXX)
Provides: Percona-XtraDB-Cluster-galera-25 galera3
Obsoletes: Percona-XtraDB-Cluster-galera-56 
Conflicts: Percona-XtraDB-Cluster-galera-2
BuildRequires:	scons check-devel glibc-devel %{gcc_req} openssl-devel %{boost_req} check-devel

%description
This package contains the Galera library required by Percona XtraDB Cluster.

%package -n Percona-XtraDB-Cluster-garbd-3
Summary:	Garbd component of Percona XtraDB Cluster
Group:		Applications/Databases
Provides:       garbd3
Requires:       %{distro_requires}
%if 0%{?systemd}
BuildRequires:  systemd
%endif
%if 0%{?systemd}
Requires(post):   systemd
Requires(preun):  systemd
Requires(postun): systemd
%else
Requires(post):   /sbin/chkconfig
Requires(preun):  /sbin/chkconfig
Requires(preun):  /sbin/service
%endif

%description -n Percona-XtraDB-Cluster-garbd-3
This package contains the garb binary and init scripts.

%prep
%setup -q -n %{src_dir}

%build
%if "%rhelver" == "5"
export CC=gcc44
export CXX=g++44
%endif
scons %{?_smp_mflags}  revno=%{galera_revision} version=%{galera_version} boost_pool=0 garb/garbd libgalera_smm.so %{scons_arch} %{scons_args}

%install
rm -rf $RPM_BUILD_ROOT
mkdir -p "$RPM_BUILD_ROOT"

install -d $RPM_BUILD_ROOT%{_sysconfdir}/{init.d,sysconfig}
install -m 644 $RPM_BUILD_DIR/%{src_dir}/garb/files/garb.cnf \
    $RPM_BUILD_ROOT%{_sysconfdir}/sysconfig/garb
install -d "$RPM_BUILD_ROOT/%{_bindir}"
install -d "$RPM_BUILD_ROOT/%{_libdir}"

%if 0%{?systemd}
install -D -m 644 $RPM_BUILD_DIR/%{src_dir}/garb/files/garb.service \
    $RPM_BUILD_ROOT/%{_unitdir}/garb.service
install -m 755 $RPM_BUILD_DIR/%{src_dir}/garb/files/garb-systemd \
    $RPM_BUILD_ROOT/%{_bindir}/garb-systemd
%else
install -m 755 $RPM_BUILD_DIR/%{src_dir}/garb/files/garb.sh \
    $RPM_BUILD_ROOT%{_sysconfdir}/init.d/garb
%endif

install -m 755 "$RPM_BUILD_DIR/%{src_dir}/garb/garbd" \
	"$RPM_BUILD_ROOT/%{_bindir}/"
install -d "$RPM_BUILD_ROOT/%{_libdir}/galera3"
install -m 755 "$RPM_BUILD_DIR/%{src_dir}/libgalera_smm.so" \
	"$RPM_BUILD_ROOT/%{_libdir}/galera3/"
ln -s "galera3/libgalera_smm.so" "$RPM_BUILD_ROOT/%{_libdir}/"

install -d $RPM_BUILD_ROOT%{docs}
install -m 644 $RPM_BUILD_DIR/%{src_dir}/COPYING                     \
    $RPM_BUILD_ROOT%{docs}/COPYING
install -m 644 $RPM_BUILD_DIR/%{src_dir}/packages/rpm/README     \
    $RPM_BUILD_ROOT%{docs}/README
install -m 644 $RPM_BUILD_DIR/%{src_dir}/packages/rpm/README-MySQL \
    $RPM_BUILD_ROOT%{docs}/README-MySQL
install -m 644 $RPM_BUILD_DIR/%{src_dir}/asio/LICENSE_1_0.txt    \
    $RPM_BUILD_ROOT{docs}/LICENSE.asio
install -m 644 $RPM_BUILD_DIR/%{src_dir}/www.evanjones.ca/LICENSE \
    $RPM_BUILD_ROOT%{docs}/LICENSE.crc32c
install -m 644 $RPM_BUILD_DIR/%{src_dir}/chromium/LICENSE       \
    $RPM_BUILD_ROOT%{docs}/LICENSE.chromium

install -d $RPM_BUILD_ROOT%{docs2}
install -m 644 $RPM_BUILD_DIR/%{src_dir}/COPYING                     \
    $RPM_BUILD_ROOT%{docs2}/COPYING
install -m 644 $RPM_BUILD_DIR/%{src_dir}/packages/rpm/README     \
    $RPM_BUILD_ROOT%{docs2}/README

install -d $RPM_BUILD_ROOT%{_mandir}
install -m 644 $RPM_BUILD_DIR/%{src_dir}/garb/files/garbd.troff  \
    $RPM_BUILD_ROOT%{_mandir}/man1/garbd.1

%clean
rm -rf $RPM_BUILD_ROOT

%files
%defattr(-,root,root,-)
# This is a symlink
%attr(0755,root,root) %{_libdir}/libgalera_smm.so
%attr(0755,root,root) %{_libdir}/galera3/libgalera_smm.so
%attr(0755,root,root) %dir %{docs}
%doc %attr(0644,root,root) %{docs}/COPYING
%doc %attr(0644,root,root) %{docs}/README
%doc %attr(0644,root,root) %{docs}/README-MySQL
%doc %attr(0644,root,root) %{docs}/LICENSE.asio
%doc %attr(0644,root,root) %{docs}/LICENSE.crc32c
%doc %attr(0644,root,root) %{docs}/LICENSE.chromium


%files -n Percona-XtraDB-Cluster-garbd-3
%defattr(-,root,root,-)
%config(noreplace,missingok) %{_sysconfdir}/sysconfig/garb
%if 0%{?systemd}
    %attr(0644, root, root) %{_unitdir}/garb.service
    %attr(0755,root,root) %{_bindir}/garb-systemd
%else 
    %attr(0755,root,root) %{_sysconfdir}/init.d/garb
%endif
%attr(0755,root,root) %{_bindir}/garbd
%doc %attr(0644,root,root) %{docs2}/COPYING
%doc %attr(0644,root,root) %{docs2}/README
%doc %attr(644, root, man) %{_mandir}/man1/garbd.1*

%post -n Percona-XtraDB-Cluster-garbd-3
%if 0%{?systemd}
  %systemd_post garb
%endif

%preun -n Percona-XtraDB-Cluster-garbd-3
%if 0%{?systemd}
    %systemd_preun garb
%endif

%postun -n Percona-XtraDB-Cluster-garbd-3
%if 0%{?systemd}
    %systemd_postun_with_restart garb
%endif

%changelog
* Thu May 15 2014 Raghavendra Prabhu <raghavendra.prabhu@percona.com>
- Split the packaging for garbd.
- Library is now installed in /usr/lib/galera2 with a symlink to /usr/lib/ for compatibility.
- Few cleanups.
