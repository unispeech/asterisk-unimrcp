FROM centos:7.8.2003

RUN yum groupinstall -y "Development Tools" && \
    yum install -y \
        sudo \
        cmake

# Install deps do unimrcp
RUN cd /tmp/ && \
    curl https://www.unimrcp.org/project/component-view/unimrcp-deps-1-6-0-tar-gz/download --output unimrcp-deps-1.6.0.tar.gz && \
    tar -zxvf unimrcp-deps-1.6.0.tar.gz && \
    cd unimrcp-deps-1.6.0 && \
    ./build-dep-libs.sh --silent

# Install unimrcp
RUN cd /tmp/ && \
    git clone https://github.com/unispeech/unimrcp.git && \
    cd unimrcp && \
    git checkout tags/unimrcp-1.7.0 && \
    ./bootstrap && \
    ./configure && \
    make -j 4 && \
    make install

# Asterisk
RUN cd /tmp/ && \
    git clone https://github.com/asterisk/asterisk.git

RUN cd /tmp/asterisk/ && \
    git checkout certified/16.8-cert2 && \
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
    git clone https://github.com/unispeech/asterisk-unimrcp.git && \
    cd asterisk-unimrcp && \
    git checkout tags/asterisk-unimrcp-1.7.0 && \
    ./bootstrap && \
    ./configure --with-asterisk-version=16.8.0 && \
    make && \
    make install
    # ./configure --with-asterisk=/tmp/asterisk/ --with-asterisk-version=17.5.0 && \

RUN localedef -i en_US -f UTF-8 en_US.UTF-8
ENV LANG=en_US.UTF-8

COPY start.sh /opt/start.sh

ENTRYPOINT ["/opt/start.sh"]

CMD ["asterisk", "-vvvdddf", "-T", "-p"]

LABEL modules="unimrcp-deps:1.6.0,unimrcp:1.7.0,asterisk:certified/16.8-cert2,asterisk-unimrcp:1.7.0"
