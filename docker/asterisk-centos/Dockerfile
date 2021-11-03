FROM centos:7.8.2003

ARG asteriskversion
ARG asteriskurl

RUN yum groupinstall -y "Development Tools" && \
    yum install -y \
        sudo \
        wget \
        rpm-build \
        cmake

RUN mkdir -p /root/rpmbuild/SPECS

COPY SPECS /root/rpmbuild/SPECS

RUN mkdir /root/rpmbuild/SOURCES && \
    cd /root/rpmbuild/SOURCES &&    \
    wget ${asteriskurl}/asterisk-${asteriskversion}-current.tar.gz

RUN localedef -i en_US -f UTF-8 en_US.UTF-8
ENV LANG=en_US.UTF-8

WORKDIR /root/rpmbuild/SPECS

CMD ["rpmbuild", "--clean", "-ba", "asterisk.spec"]

LABEL modules="centos:7.8.2003:rpm-build,asterisk-certified-16.8-src"
