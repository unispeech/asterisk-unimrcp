FROM centos:7.8.2003

RUN yum groupinstall -y "Development Tools" && \
    yum install -y \
        sudo \
        cmake

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

RUN localedef -i en_US -f UTF-8 en_US.UTF-8
ENV LANG=en_US.UTF-8

COPY start.sh /opt/start.sh

ENTRYPOINT ["/opt/start.sh"]

CMD ["asterisk", "-vvvdddf", "-T", "-p"]

LABEL modules="asterisk:certified/16.8-cert2"
