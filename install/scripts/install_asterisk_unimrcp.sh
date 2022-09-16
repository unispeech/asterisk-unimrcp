#/bin/bash

UNIMRCP_PATH="/usr/local/unimrcp"
LIB_APR_PATH="/usr/local/apr"

#cp -vr . /
if [[ -d "/etc/asterisk" ]]; then
  rsync -av ./ / --exclude etc
  echo;echo 'Files in "/etc/asterisk" were not updated!!!';echo
else
  rsync -av ./ /
  echo "New files in \"/etc/asterisk\"."
fi

# Create libraries symlinks
cd $UNIMRCP_PATH/lib && bash lib_map && rm lib_map
cd $LIB_APR_PATH/lib && bash lib_map && rm lib_map

