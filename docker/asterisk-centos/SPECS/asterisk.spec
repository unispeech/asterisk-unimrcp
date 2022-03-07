Name:		asterisk
Version:	16.8
Release:	0
Summary:	Asterisk - Open Source PBX and telephony toolkit.

Group:		Internet Applications
License:	GPL
URL:		https://www.asterisk.org
Source0:	http://downloads.asterisk.org/pub/telephony/certified-asterisk/asterisk-certified-16.8-current.tar.gz

#BuildRequires:  Development Tools, cmake, sudo	
#Requires:	

%description
Asterisk is an Open Source PBX and telephony toolkit.
It is, in a sense, middleware between Internet and
telephony channels on the bottom, and Internet and
telephony applications at the top.

Asterisk can be used with Voice over IP (SIP, H.323, IAX and more)
standards, or the Public Switched Telephone Network (PSTN)
through supported hardware. 

%prep
%setup -q -n %{name}-certified-%{version}-cert12
./bootstrap.sh && cd contrib/scripts && ./install_prereq install && ./install_prereq install-unpackaged

%build
./configure --with-jansson-bundled
make

%install
make DESTDIR=$RPM_BUILD_ROOT install
make DESTDIR=$RPM_BUILD_ROOT samples
make DESTDIR=$RPM_BUILD_ROOT config
make DESTDIR=$RPM_BUILD_ROOT install-logrotate

%clean
[ "${RPM_BUILD_ROOT}" != "/" ] && rm -rf ${RPM_BUILD_ROOT}

%files
%defattr(-,root,root,-)
%doc README.md README-SERIOUSLY.bestpractices.md LICENSE CHANGES ChangeLog
/var/lib/asterisk/
/etc/asterisk/
/etc/logrotate.d/asterisk
/usr/include/asterisk.h
/usr/include/asterisk
/usr/lib/asterisk/
/usr/lib/libasteriskpj.so
/usr/lib/libasteriskpj.so.2
/usr/lib/libasteriskssl.so
/usr/lib/libasteriskssl.so.1
/usr/sbin/astcanary
/usr/sbin/astdb2bdb
/usr/sbin/astdb2sqlite3
/usr/sbin/asterisk
/usr/sbin/astgenkey
/usr/sbin/astversion
/usr/sbin/autosupport
/usr/sbin/rasterisk
/usr/sbin/safe_asterisk
/usr/share/man/man8/
/var/spool/asterisk/

%changelog

