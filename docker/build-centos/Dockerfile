FROM centos:7.8.2003

RUN yum groupinstall -y "Development Tools" && \
    yum install -y \
        sudo \
        cmake

ARG UNIMRCP_DEPS=1.6.0
ARG UNIMRCP_VER=1.7.0
ARG ASTERISK_VER=16.8.0
ARG ASTERISK_MRCP_BRANCH=master

# Install deps do unimrcp
RUN UNIMRCP_DEPS_DASH=$(echo ${UNIMRCP_DEPS} | tr . -) && cd /tmp/ && \
    curl https://www.unimrcp.org/project/component-view/unimrcp-deps-${UNIMRCP_DEPS_DASH}-tar-gz/download --output unimrcp-deps-${UNIMRCP_DEPS}.tar.gz && \
    tar -zxvf unimrcp-deps-${UNIMRCP_DEPS}.tar.gz && \
    cd unimrcp-deps-${UNIMRCP_DEPS} && \
    ./build-dep-libs.sh --silent

# Install unimrcp
RUN cd /tmp/ && \
    git clone https://github.com/unispeech/unimrcp.git && \
    cd unimrcp && \
    git checkout tags/unimrcp-${UNIMRCP_VER} && \
    ./bootstrap && \
    ./configure && \
    make -j 4 && \
    make install

# Asterisk
RUN cd /tmp/ && \
    git clone https://github.com/asterisk/asterisk.git

RUN cd /tmp/asterisk/ && \
    git checkout ${ASTERISK_VER} && \
    ./bootstrap.sh && \
    cd contrib/scripts/ && \
    ./install_prereq install && \
    ./install_prereq install-unpackaged

RUN cd /tmp/asterisk/ && \
    ./configure --with-jansson-bundled && \
    make && \
    make install && \
    make samples && \
    make config && \
    make install-logrotate

# Asterisk-unimrcp
RUN cd /tmp/ && \
    git clone https://github.com/CPqD/asterisk-unimrcp.git && \
    cd asterisk-unimrcp && \
    git checkout ${ASTERISK_MRCP_BRANCH} && \
    ./bootstrap && \
    ./configure --with-asterisk-version=${ASTERISK_VER} && \
    make && \
    make install
    # ./configure --with-asterisk=/tmp/asterisk/ --with-asterisk-version=17.5.0 && \

RUN localedef -i en_US -f UTF-8 en_US.UTF-8
ENV LANG=en_US.UTF-8

COPY start.sh /opt/start.sh

ENTRYPOINT ["/opt/start.sh"]

CMD ["asterisk", "-vvvdddf", "-T", "-p"]

LABEL modules="centos:7.8,unimrcp-deps:${UNIMRCP_DEPS},unimrcp:${UNIMRCP_VER},asterisk:${ASTERISK_VER},asterisk-unimrcp:${UNIMRCP_VER}"
