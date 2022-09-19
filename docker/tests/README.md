#Prepare images for test

## Build asterisk in a docker container
In folder docker/build-centos:
docker build -t <asterisk unimrcp build name> --build-arg ASTERISK_VER=<asterisk version> --build-arg ASTERISK_MRCP_BRANCH=<asterisk unimrcp branch> .

Example:
docker build -t asterisk-unimrcp-build:Asterisk-13.18.1-BMT-657 --build-arg ASTERISK_VER=13.18.1 --build-arg ASTERISK_MRCP_BRANCH=feature/BMT-657 .

## Create astrerisk-unimrcp installer
docker run --rm -v /tmp/asterisk-unimrcp:/src --name asterisk-build asterisk-unimrcp-build:Asterisk-13.18.1-BMT-657 /src/install/make_asterisk_unimrcp_install.sh install_asterisk_unimrcp 13.18.1

## Create a image for test execution in folder docker/exec-centos
In folder docker/exec-centos:
docker build -t <asterisk unimrcp exec name> --build-arg BASE_IMAGE=<asterisk unimrcp build name> .

Example:
docker docker build -t asterisk-unimrcp-exec:Asterisk-13.18.1-BMT-657 --build-arg BASE_IMAGE=asterisk-unimrcp-build:Asterisk-13.18.1-BMT-657 .


## Goto docker test directory
cd <asterisk-unimrcp repository path>/docker/tests

## Build image for run test
docker build -t asterisk-test:1.0  .

#Running test

## Configure MRCP server ip with commands:
sed -i s/__SERVER_IP__/<MRCP Server Ip>/g res/etc/asterisk/mrcp.conf
sed -i s/__SERVER_IP__/<MRCP Server Ip>/g res/usr/local/unimrcp/conf/client-profiles/cpqd.xml

Example:
sed -i s/__SERVER_IP__/192.168.25.153/g res/etc/asterisk/mrcp.conf
sed -i s/__SERVER_IP__/192.168.25.153/g res/usr/local/unimrcp/conf/client-profiles/cpqd.xml

---
**Note**
The MRCP server must be installed and configure with ou plugins: SYNTHETIZER, RECOGNIZER, VERIFY.
The TTS, ASR and Biometric server should be available and connected to MRCP Server.
---

## Fix repository base path in .env
REPO_PATH="The path without asterisk-unimrcp folder"

## Check parameters in .env
REPO_PATH="/tmp"
ASTERISK_SUBNET="172.16.1"
ASTERISK_IMAGE=asterisk-unimrcp-exec:Asterisk-13.18.1-BMT-657
TEST_IMAGE=asterisk-test:1.0
ASTERISK_CONTAINER="asterisk-runner"
TEST_CONTAINER="asterisk-test"
RUN_TEST="./test-script.sh all"

## Run test
docker-compose --profile test  up

## Running stress test

### Change TEST_RUN in .env

RUN_TEST="./test-stress.sh

### Run load test
docker-compose --profile test  up
