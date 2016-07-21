%define name     rem
%define ver      0.4.6
%define rel      1

Summary: Audio and Video processing media library
Name: %name
Version: %ver
Release: %rel
License: BSD
Group: Applications/Devel
Source0: file://%{name}-%{version}.tar.gz
URL: http://www.creytiv.com/
Vendor: Creytiv
Packager: Alfred E. Heggestad <aeh@db.org>
BuildRoot: /var/tmp/%{name}-build-root
BuildRequires: re-devel

%description
Audio and Video processing media library

%package devel
Summary:	librem development files
Group:		Development/Libraries
Requires:	%{name} = %{version}

%description devel
librem development files.

%prep
%setup

%build
make RELEASE=1

%install
rm -rf $RPM_BUILD_ROOT
make DESTDIR=$RPM_BUILD_ROOT install \
%ifarch x86_64
	LIBDIR=/usr/lib64
%endif

%clean
rm -rf $RPM_BUILD_ROOT

%post -p /sbin/ldconfig
%postun -p /sbin/ldconfig

%files
%defattr(644,root,root,755)
%attr(755,root,root) %{_libdir}/librem*.so*

%files devel
%defattr(644,root,root,755)
%{_includedir}/rem/*.h
%{_libdir}/librem*.a


%changelog
* Wed Sep 7 2011 Alfred E. Heggestad <aeh@db.org> -
- Initial build.

