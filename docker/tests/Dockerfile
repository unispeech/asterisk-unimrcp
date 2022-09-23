FROM amd64/ubuntu:20.04

RUN apt-get update && \
    apt-get -y install \
    ca-certificates \
    wget \
    lsb-release \
    gnupg

# Sip Simple repository
RUN echo "deb       http://ag-projects.com/ubuntu `lsb_release -c -s` main" >> /etc/apt/sources.list
RUN cd /tmp && wget http://download.ag-projects.com/agp-debian-gpg.key && apt-key add agp-debian-gpg.key && rm agp-debian-gpg.key

# Docker package
RUN mkdir -p /etc/apt/keyrings && wget -O- https://download.docker.com/linux/ubuntu/gpg | gpg --dearmor -o /etc/apt/keyrings/docker.gpg
RUN  echo \
       "deb [arch=$(dpkg --print-architecture) signed-by=/etc/apt/keyrings/docker.gpg] https://download.docker.com/linux/ubuntu \
       $(lsb_release -cs) stable" | tee /etc/apt/sources.list.d/docker.list > /dev/null

RUN apt-get update && \
    apt-get -y install \
    docker-ce-cli \
    sudo \
    tshark \
    python3 \
    python3-sipsimple && \
    apt-get autoremove -y

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get -y install tzdata
RUN apt-get -y install locales

RUN localedef -i en_US -f UTF-8 en_US.UTF-8
ENV LANG=en_US.UTF-8

COPY test-script.sh /opt/test-script.sh
COPY sip-caller.py /opt/sip-caller.py
COPY config /opt/config
COPY audios /opt/audios

RUN chmod +x /opt/test-script.sh /opt/sip-caller.py

ENTRYPOINT ["/opt/test-script.sh", "all"]

LABEL modules="ubuntu:20.04,python3,sipsimple"
