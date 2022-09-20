

# Executing tests with Asterisk Unimrcp

## Test description

The objective of the test is validate scenarios for Speech Synthesis, Speech Recognition and Speaker Verification. To achieve such objectives the following test scenario was built:
```
                                                         +------------+
                                                    +--->| TTS Server |
                                                    |    +------------+
+--------+        +----------+        +--------+    |    +------------+
|  Test  |        |          |        |  MRCP  |----+--->| ASR Server |
|        |------->| Asterisk |------->|        |    |    +------------+
|  Orch. |        |          |        | Server |    |    +------------+
+---+----+        +-----^----+        +--------+    +--->| BIO Server |
    |   (docker cmds)   |                                +------------+
    +-------------------+
```
The code presents here build the containers for Test Orchestrator and Asterisk. The MRCP, TTS, ASR and Biometric servers should be installed and configured externally.
The Test Orchestrator contains a SIP caller script based on [SIP Simple](https://sipsimpleclient.org/), and shell scripts to orchestrate the test: 
- test-script.sh, which orchestrates test scenarios
- test-load.sh, which orchestrates load test
The following test scenarios are available:

 Dialplan number     | Test case
|--------------------|--------------------
|301, 302            | Recognition follow by Verification
|303, 305            | Recognition follow by Clear-Buffer
|304                 | Buffer discard (rollback)
|306                 | Double buffer discard (rollback)
|306                 | Double buffer discard (rollback)
|308                 | Single Recognition
|308                 | Multiple Recognitions
|309                 | Single Verification
|310                 | Multiple Verification
|311                 | Recognition and verification
|312                 | Single Synthesis
|313                 | Synthesis and Recognition
|314                 | Synthesis follow by Recognition
|315                 | Synthesis follow by Recognition follow by Verify
|315                 | Synthesis follow by Recognition follow by Verify
|316                 | Seq. for separated Synthesis, Recognition and Verify
|317                 | Seq. for separated Recognition and Verify
|318                 | Seq. for separated enrollments

## Prepare images for test

### Build asterisk-unimrcp in a docker container

#### Prepare image for build with an Asterisk version
In folder docker/build-centos:
```
docker build -t <asterisk unimrcp build image name> --build-arg ASTERISK_VER=<asterisk version> --build-arg ASTERISK_MRCP_BRANCH=<asterisk unimrcp branch> .
```
Example:
```
docker build -t asterisk-unimrcp-build:Asterisk-13.18.1-BMT-657 --build-arg ASTERISK_VER=13.18.1 --build-arg ASTERISK_MRCP_BRANCH=feature/BMT-657 .
```
#### Create astrerisk-unimrcp installer
```
docker run --rm -v <path to asterisk-unimrcp code>:/src <Image for build>  /src/install/make_asterisk_unimrcp_install.sh <asterisk unimrcp installer name> <asterisk version>
```
Example:
```
docker run --rm -v /tmp/asterisk-unimrcp:/src --name asterisk-build asterisk-unimrcp-build:Asterisk-13.18.1-BMT-657 /src/install/make_asterisk_unimrcp_install.sh install_asterisk_unimrcp 13.18.1
```
### Create a image for test execution
In folder docker/exec-centos:
```
docker build -t <asterisk unimrcp exec name> --build-arg BASE_IMAGE=<asterisk unimrcp build image name> .
```
Example:
```
docker docker build -t asterisk-unimrcp-exec:Asterisk-13.18.1-BMT-657 --build-arg BASE_IMAGE=asterisk-unimrcp-build:Asterisk-13.18.1-BMT-657 .
```
### Build image for run test

 Goto docker test directory

```
cd <asterisk-unimrcp repository path>/docker/tests
```
Build image
```
docker build -t asterisk-test:1.0  .
```
## Running test

### Configure MRCP server ip with commands:
```
sed -i s/__SERVER_IP__/<MRCP Server Ip>/g res/etc/asterisk/mrcp.conf
sed -i s/__SERVER_IP__/<MRCP Server Ip>/g res/usr/local/unimrcp/conf/client-profiles/cpqd.xml
```
Example:
```
sed -i s/__SERVER_IP__/192.168.25.153/g res/etc/asterisk/mrcp.conf
sed -i s/__SERVER_IP__/192.168.25.153/g res/usr/local/unimrcp/conf/client-profiles/cpqd.xml
```
---

**Note**
- The MRCP server must be installed and configure with ou plugins: SYNTHETIZER, RECOGNIZER, VERIFY
- The TTS, ASR and Biometric server should be available and connected to MRCP Server
---

### Check parameters in .env
```
REPO_PATH=$HOME
ASTERISK_SUBNET="172.16.1"
ASTERISK_IMAGE=asterisk-unimrcp-exec:Asterisk-13.18.1-BMT-657
ASTERISK_UNIMRCP_INSTALLER="install_asterisk_unimrcp"
TEST_IMAGE=asterisk-test:1.0
ASTERISK_CONTAINER="asterisk-runner"
TEST_CONTAINER="asterisk-test"
RUN_TEST="./test-script.sh all"
```
---
**Note**
1. Repository path: the path for directory when asterisk-unimrcp folder is installed
2. Asterisk subnet: choose a subnet not in use
3. Asterisk Image: use the image build for testing
4. Asterisk Unimrcp installer: use the installer name create in above step
---
### Run test
```
docker-compose --profile test  up
```
### Running load test

#### Change TEST_RUN in .env
```
RUN_TEST="./test-load.sh"
```
#### Run load test
```
docker-compose --profile test  up
```
