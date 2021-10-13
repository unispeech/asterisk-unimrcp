#!/bin/bash

Usage() {
cat <<EOF

  Usage: $0 <pack_name> [UniMRCP Asterisk module installation dir] [UniMRCP module installation dir] [build dir]

    You must provide the UniMRCP Asterisk module installation directory, if it is not in "/usr/lib/asterisk/modules"

    You must provide the UniMRCP module installation directory, if it is not in "/usr/local/unimrcp"

    You must provide the build directory, if it is not in "/opt/unimrcp"

EOF
}

[[ $# -eq 0 ]] && { Usage; exit 1; }

PACK_NAME=$1

## Directories and make package tools definition
REPO_INSTALL_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
REPO_DIR=$(dirname $REPO_INSTALL_DIR)
UNIMRCP_DEPS_DIR=/tmp/unimrcp-deps-1.6.0
UNIMRCP_DIR=/tmp/unimrcp
SCRIPTS_DIR="${REPO_INSTALL_DIR}/scripts"
INSTALL_SCRIPT_NAME="install_asterisk_unimrcp.sh"
INSTALL_SCRIPT="${SCRIPTS_DIR}/${INSTALL_SCRIPT_NAME}"
MAKESELF="${SCRIPTS_DIR}/makeself/makeself.sh"

echo $REPO_DIR

## Build dir
[[ ! -z $3 ]] && { BUILD_DIR="$REPO_DIR/$4"; } || { BUILD_DIR="$REPO_DIR"; }

## Project dependencies definition
[[ ! -z $2 ]] && { UNIMRCP_INSTALL_DIR=$3; } || { UNIMRCP_INSTALL_DIR="/usr/local/unimrcp"; }

## Project install target
[[ ! -z $2 ]] && { ASTERISK_UNIMRCP_INSTALL_DIR=$2; } || { ASTERISK_UNIMRCP_INSTALL_DIR="/usr/lib/asterisk/modules"; }
echo "REPO_INSTALL_DIR=$REPO_INSTALL_DIR"
echo "BUILD_DIR=$BUILD_DIR"
echo "REPO_DIR=$REPO_DIR"
echo "UNIMRCP_DEPS_DIR=$UNIMRCP_DEPS_DIR"
echo "UNIMRCP_DIR=$UNIMRCP_DIR"
echo "ASTERISK_UNIMRCP_INSTALL_DIR=$ASTERISK_UNIMRCP_INSTALL_DIR"
echo "UNIMRCP_INSTALL_DIR=$UNIMRCP_INSTALL_DIR"

APR_LIB_DIR=/usr/local/apr/lib
SIP_LIB_DIR=/usr/local/lib
lib_uni_version="0.7.0"
lib_sip_version="0.6.0"
lib_apr_version="0.5.2"
lib_aprutil_version="0.5.4"
if [[ -e /etc/redhat-release ]]; then
  lib_expat_version="0.5.0"
  lib_expat_system_version="1.6.0"
  is_rhel="true"
else
  is_rhel="false"
fi

cat <<EOF

  ################ Atention ###########################

  *** Change the "version variables" of this script
       if some dependency version has changed!!!!

     Using dependencies:
       libs unimrcp $lib_uni_version
       libsofia-sip-ua $lib_sip_version
       libapr $lib_apr_version
       libaprutil $lib_aprutil_version
       Looking for libexpat: $is_rhel $lib_expat_version
       Looking for libexpat from system: $is_rhel $lib_expat_system_version
  #####################################################

EOF
sleep 2; ## Give time to people look at the message.

## Building the project

# Buid Unimrcp dependencies
cd $UNIMRCP_DEPS_DIR
./build-dep-libs.sh --silent

ret=$(echo $?)
[[ $ret -ne 0 ]] && { echo "Please check your UNIMRCP Dependencies build."; exit 1; }

# Build Unimrcp
cd $UNIMRCP_DIR
./bootstrap && ./configure && make -j 4 && make install

ret=$(echo $?)
[[ $ret -ne 0 ]] && { echo "Please check your UNIMRCP build."; exit 1; }

# Build Asterisk Unimrcp
cd $BUILD_DIR
./bootstrap && ./configure --with-asterisk-version=16.8.0 && make && make install

ret=$(echo $?)
[[ $ret -ne 0 ]] && { echo "Please check your ASTERISK UNIMRCP Dependencies build."; exit 1; }


## Creating the installation package
cd $REPO_INSTALL_DIR

# Directory used to create the installation package
TMP_PATH="${REPO_INSTALL_DIR}/mrcp_to_install"
[[ ! -d ${TMP_PATH} ]] && { mkdir -p ${TMP_PATH}; } || { rm -rf ${TMP_PATH}/*; }

mkdir -p ${TMP_PATH}/$UNIMRCP_INSTALL_DIR/../apr/lib
mkdir -p ${TMP_PATH}/$UNIMRCP_INSTALL_DIR/plugin
mkdir -p ${TMP_PATH}/$UNIMRCP_INSTALL_DIR/log
mkdir -p ${TMP_PATH}/$UNIMRCP_INSTALL_DIR/lib
mkdir -p ${TMP_PATH}/$UNIMRCP_INSTALL_DIR/var
mkdir -p ${TMP_PATH}/$ASTERISK_UNIMRCP_INSTALL_DIR
mkdir -p ${TMP_PATH}/$UNIMRCP_INSTALL_DIR/bin


# The CPqD MRCP stuff that will be installed
README="$REPO_INSTALL_DIR/resources/README.md"

ASTERISK_UNIMRCP_LIB_SO="$(readlink -e $ASTERISK_UNIMRCP_INSTALL_DIR/res_speech_unimrcp.so)"
ASTERISK_UNIMRCP_LIB_LA="$(readlink -e $ASTERISK_UNIMRCP_INSTALL_DIR/res_speech_unimrcp.la)"
ASTERISK_UNIMRCP_APP_SO="$(readlink -e $ASTERISK_UNIMRCP_INSTALL_DIR/app_unimrcp.so)"
ASTERISK_UNIMRCP_APP_LA="$(readlink -e $ASTERISK_UNIMRCP_INSTALL_DIR/app_unimrcp.la)"
#CPQD_VER_PLUGIN="$(readlink -e $BUILD_DIR/installed/plugin/cpqd_mrcp_ver.so)"
ASTERISK_MRCP_CONF_DIR="$REPO_INSTALL_DIR/resources/usr"
ASTERISK_ETC_DIR="$REPO_INSTALL_DIR/resources/etc"

# The UniMRCP, Sophia e Apr stuff that will be installed
UNIMRCP_SERVER="$UNIMRCP_INSTALL_DIR/bin/unimrcpserver"
UNIMRCP_SERVER_LIB="$UNIMRCP_INSTALL_DIR/lib/libunimrcpserver.so.$lib_uni_version"
LIB_SIP="$SIP_LIB_DIR/libsofia-sip-ua.so.$lib_sip_version"
LIB_APR="$APR_LIB_DIR/libapr-1.so.$lib_apr_version"
LIB_APR_UTIL="$APR_LIB_DIR/libaprutil-1.so.$lib_aprutil_version"
UNIMRCP_CLIENT_LIB="$UNIMRCP_INSTALL_DIR/lib/libunimrcpclient.so.$lib_uni_version"
UNIMRCP_CLIENT_UMC="$UNIMRCP_INSTALL_DIR/bin/umc"

# Copy Asterisk Unimrcp components
[[ -e ${ASTERISK_UNIMRCP_LIB_SO} ]] && {  cp ${ASTERISK_UNIMRCP_LIB_SO} ${TMP_PATH}/$ASTERISK_UNIMRCP_INSTALL_DIR; } ||
  { echo "Missing ${ASTERISK_UNIMRCP_LIB_SO} Exiting..."; exit 1; }

[[ -e ${ASTERISK_UNIMRCP_LIB_LA} ]] && {  cp ${ASTERISK_UNIMRCP_LIB_LA} ${TMP_PATH}/$ASTERISK_UNIMRCP_INSTALL_DIR; } ||
  { echo "Missing ${ASTERISK_UNIMRCP_LIB_LA} Exiting..."; exit 1; }

[[ -e ${ASTERISK_UNIMRCP_APP_SO} ]] && {  cp ${ASTERISK_UNIMRCP_APP_SO} ${TMP_PATH}/$ASTERISK_UNIMRCP_INSTALL_DIR; } ||
  { echo "Missing ${ASTERISK_UNIMRCP_APP_SO} Exiting..."; exit 1; }

[[ -e ${ASTERISK_UNIMRCP_APP_LA} ]] && {  cp ${ASTERISK_UNIMRCP_APP_LA} ${TMP_PATH}/$ASTERISK_UNIMRCP_INSTALL_DIR; } ||
  { echo "Missing ${ASTERISK_UNIMRCP_APP_LA} Exiting..."; exit 1; }

# Copy configuration files
{ cp -r ${ASTERISK_MRCP_CONF_DIR} ${TMP_PATH}/; } || { echo "Fail to copy MRCP configuration: ${ASTERISK_MRCP_CONF_DIR}"; exit 1; }
{ cp -r ${ASTERISK_ETC_DIR} ${TMP_PATH}/; } || { echo "Fail to copy Asterisk configuration: ${ASTERISK_ETC_DIR}"; exit 1; }

# Copy Unimrcp components
for unimrcp_element in $UNIMRCP_SERVER  $UNIMRCP_CLIENT_UMC; do
  [[ -e ${unimrcp_element} ]] && {  cp ${unimrcp_element} ${TMP_PATH}/$UNIMRCP_INSTALL_DIR/bin/; } ||
    { echo "Missing some UniMRCP bin ($unimrcp_element). Exiting..."; exit 1; }
done

for unimrcp_element in $UNIMRCP_SERVER_LIB $LIB_SIP $UNIMRCP_CLIENT_LIB; do
  [[ -e ${unimrcp_element} ]] && {  cp ${unimrcp_element} ${TMP_PATH}/$UNIMRCP_INSTALL_DIR/lib/; } ||
    { echo "Missing some UniMRCP related lib ($unimrcp_element). Exiting..."; exit 1; }
done

for unimrcp_element in $LIB_APR $LIB_APR_UTIL; do
  [[ -e ${unimrcp_element} ]] && {  cp ${unimrcp_element} ${TMP_PATH}/${UNIMRCP_INSTALL_DIR}/../apr/lib; } ||
    { echo "Missing some Apr lib ($unimrcp_element). Exiting..."; exit 1; }
done

# Copy installation script
[[ -e ${INSTALL_SCRIPT} ]] && { cp ${INSTALL_SCRIPT} ${TMP_PATH}/${UNIMRCP_INSTALL_DIR}/bin/; } ||
  { echo "Missing ${INSTALL_SCRIPT}. Exiting..."; exit 1; }

# Copy README file
[[ -e ${README} ]] && { cp ${README} ${TMP_PATH}/${UNIMRCP_INSTALL_DIR}; } ||
  { echo "Missing README file (${README}). Exiting..."; exit 1; }

# Mapping the libraries versions to make symlink
## Server dependencies
echo "" >${TMP_PATH}/$UNIMRCP_INSTALL_DIR/lib/lib_map
echo "ln -s libunimrcpserver.so.$lib_uni_version \
        libunimrcpserver.so.$(awk -F'.' '{print $1"."$2}' <<<$lib_uni_version)" >>${TMP_PATH}/$UNIMRCP_INSTALL_DIR/lib/lib_map
echo "ln -s libunimrcpserver.so.$(awk -F'.' '{print $1"."$2}' <<<$lib_uni_version) \
        libunimrcpserver.so.$(awk -F'.' '{print $1}' <<<$lib_uni_version)" >>${TMP_PATH}/$UNIMRCP_INSTALL_DIR/lib/lib_map
echo "ln -s libsofia-sip-ua.so.$lib_sip_version \
        libsofia-sip-ua.so.$(awk -F'.' '{print $1"."$2}' <<<$lib_sip_version)" >>${TMP_PATH}/$UNIMRCP_INSTALL_DIR/lib/lib_map
echo "ln -s libsofia-sip-ua.so.$(awk -F'.' '{print $1"."$2}' <<<$lib_sip_version) \
        libsofia-sip-ua.so.$(awk -F'.' '{print $1}' <<<$lib_sip_version)" >>${TMP_PATH}/$UNIMRCP_INSTALL_DIR/lib/lib_map
echo "" >${TMP_PATH}/$UNIMRCP_INSTALL_DIR/../apr/lib/lib_map
echo "ln -s libaprutil-1.so.$lib_aprutil_version \
        libaprutil-1.so.$(awk -F'.' '{print $1"."$2}' <<<$lib_aprutil_version)" >>${TMP_PATH}/$UNIMRCP_INSTALL_DIR/../apr/lib/lib_map
echo "ln -s libaprutil-1.so.$(awk -F'.' '{print $1"."$2}' <<<$lib_aprutil_version) \
        libaprutil-1.so.$(awk -F'.' '{print $1}' <<<$lib_aprutil_version)" >>${TMP_PATH}/$UNIMRCP_INSTALL_DIR/../apr/lib/lib_map
echo "ln -s libapr-1.so.$lib_apr_version \
        libapr-1.so.$(awk -F'.' '{print $1"."$2}' <<<$lib_apr_version)" >>${TMP_PATH}/$UNIMRCP_INSTALL_DIR/../apr/lib/lib_map
echo "ln -s libapr-1.so.$(awk -F'.' '{print $1"."$2}' <<<$lib_apr_version) \
        libapr-1.so.$(awk -F'.' '{print $1}' <<<$lib_apr_version)" >>${TMP_PATH}/$UNIMRCP_INSTALL_DIR/../apr/lib/lib_map
### Tools dependencies
echo "" >>${TMP_PATH}/$UNIMRCP_INSTALL_DIR/lib/lib_map
echo "ln -s libunimrcpclient.so.$lib_uni_version \
        libunimrcpclient.so.$(awk -F'.' '{print $1"."$2}' <<<$lib_uni_version)" >>${TMP_PATH}/$UNIMRCP_INSTALL_DIR/lib/lib_map
echo "ln -s libunimrcpclient.so.$(awk -F'.' '{print $1"."$2}' <<<$lib_uni_version) \
        libunimrcpclient.so.$(awk -F'.' '{print $1}' <<<$lib_uni_version)" >>${TMP_PATH}/$UNIMRCP_INSTALL_DIR/lib/lib_map

# Library only needed in RHEL based Distro:
if [[ $is_rhel == "true" ]]; then
   LIB_APR_EXPAT="$APR_LIB_DIR/libexpat.so.$lib_expat_version"
   LIB64_EXPAT="/usr/lib64/libexpat.so.$lib_expat_system_version"

  if [[ -e ${LIB_APR_EXPAT} ]]; then
    cp ${LIB_APR_EXPAT} ${TMP_PATH}/$UNIMRCP_INSTALL_DIR/../apr/lib
    echo "ln -s libexpat.so.$lib_expat_version \
          libexpat.so.$(awk -F'.' '{print $1"."$2}' <<<$lib_expat_version)" >>${TMP_PATH}/$UNIMRCP_INSTALL_DIR/../apr/lib/lib_map
    echo "ln -s libexpat.so.$(awk -F'.' '{print $1"."$2}' <<<$lib_expat_version) \
          libexpat.so.$(awk -F'.' '{print $1}' <<<$lib_expat_version)" >>${TMP_PATH}/$UNIMRCP_INSTALL_DIR/../apr/lib/lib_map
  elif [[ -e ${LIB64_EXPAT} ]]; then
    echo "Getting $LIB64_EXPAT. lib_expat: $LIB_APR_EXPAT, is missing."
    cp ${LIB64_EXPAT} ${TMP_PATH}/$UNIMRCP_INSTALL_DIR/../apr/lib
    echo "ln -s libexpat.so.$lib_expat_system_version \
          libexpat.so.$(awk -F'.' '{print $1"."$2}' <<<$lib_expat_system_version)" >>${TMP_PATH}/$UNIMRCP_INSTALL_DIR/../apr/lib/lib_map
    echo "ln -s libexpat.so.$(awk -F'.' '{print $1"."$2}' <<<$lib_expat_system_version) \
          libexpat.so.$(awk -F'.' '{print $1}' <<<$lib_expat_system_version)" >>${TMP_PATH}/$UNIMRCP_INSTALL_DIR/../apr/lib/lib_map
  else
    echo "Missing lib_expat. Exiting..."
    exit 1
  fi

fi

echo "Running MakeSelf"

makeself_args="--follow"

$MAKESELF "${makeself_args}" "${TMP_PATH}" "${PACK_NAME}.run" "${PACK_NAME}" "./$UNIMRCP_INSTALL_DIR/bin/${INSTALL_SCRIPT_NAME}"

rm -rf ${TMP_PATH}

echo "All done."
exit 0;
