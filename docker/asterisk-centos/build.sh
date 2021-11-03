#!/bin/sh

asteriskversion="certified-16.8"
asteriskurl="http://downloads.asterisk.org/pub/telephony/certified-asterisk/"
dockversion="centos7.8:rpm-build-${asteriskversion}"

if ! docker build \
    --build-arg asteriskversion=$asteriskversion \
    --build-arg asteriskurl=$asteriskurl \
    -t $dockversion \
    -f Dockerfile \
    .
then
    ret=$?
    echo "fail" >&2
    exit $?
fi

[ ! -d RPMS ] && mkdir RPMS

docker run --rm -v $PWD/RPMS:/root/rpmbuild/RPMS $dockversion 
