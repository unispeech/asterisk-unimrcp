ARG BASE_IMAGE=asterisk-unimrcp-build
FROM $BASE_IMAGE

RUN yum install -y \
    tcpdump

RUN localedef -i en_US -f UTF-8 en_US.UTF-8
ENV LANG=en_US.UTF-8

COPY start.sh /opt/start.sh

ENTRYPOINT ["/opt/start.sh"]

CMD ["asterisk", "-vvvdddf", "-T", "-p"]

LABEL modules="centos:7.8,asterisk:${ASTERISK_VER}"
