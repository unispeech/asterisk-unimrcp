#/bin/bash

UNIMRCP_PATH="/usr/local/unimrcp"
LIB_APR_PATH="/usr/local/apr"

cp -vr . /

# Create libraries symlinks
cd $UNIMRCP_PATH/lib && bash lib_map && rm lib_map
cd $LIB_APR_PATH/lib && bash lib_map && rm lib_map

