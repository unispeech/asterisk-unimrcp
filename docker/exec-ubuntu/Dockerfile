FROM amd64/ubuntu:18.04

RUN apt-get update && \
    apt-get -y install \
    automake \
    autoconf \
    sudo \
    libtool \
    git \
    curl \
    wget \
    libnewt-dev \
    libssl-dev \
    libncurses5-dev \
    subversion \
    libsqlite3-dev \
    build-essential \
    libjansson-dev \
    libxml2-dev \
    uuid-dev && \
    apt-get autoremove -y

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get -y install tzdata
RUN apt-get -y install locales

ARG ASTERISK_VER=18.7.1

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

RUN make install-headers || echo "WARNING no-install-headers"

RUN localedef -i en_US -f UTF-8 en_US.UTF-8
ENV LANG=en_US.UTF-8

COPY start.sh /opt/start.sh

ENTRYPOINT ["/opt/start.sh"]

CMD ["asterisk", "-vvvdddf", "-T", "-p"]

LABEL modules="ubuntu:18.04,asterisk:${ASTERISK_VER}
