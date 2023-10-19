#!/bin/bash

AUDIO_PATH="/opt/audios"
ASTERISK_CONTAINER="asterisk-runner"
SIP_SERVER="${ASTERISK_CONTAINER}"
RESULT_FILE="/tmp/test.txt"
VALIDATE_FILE="/tmp/test2.txt"
CAPTURE_FILE="/tmp/cap.txt"
FINAL_RESULT=0
TEST_LOG="/tmp/test.log"

log()
{
  echo $@ >> $TEST_LOG
}

run-test()
{
  local audio=$1
  local extension=$2
  python3 sip-caller.py -f ${AUDIO_PATH}/$audio -t sip:${extension}@${SIP_SERVER}
}

validate()
{
  local key=$1
  local val=$2
  local file=$3
  local error=0

  V=$(grep -c "$key\$" $file)
  [[ $V == $val ]] || error=1
  log "Validate message $key V=$V"
  echo $error
}

validate-result()
{
  local recogs=$1
  local results=$2
  local synths=$3
  local error=0
  V=$(grep -c interpretation ${VALIDATE_FILE});
  [[ $V == $recogs ]] || { error=1; log "Interpretation error: $V"; }
  V=$(grep -c VERIF_RESULT ${VALIDATE_FILE});
  [[ $V == $results ]] || { error=1; log "Verify reasult error: $V"; }
  V=$(grep -cP 'id="\d{18}"' ${VALIDATE_FILE});
  [[ $V == $results ]] || { error=1; log "Verify result content error: $V"; }
  V=$(grep -c SYNTHSTATUS=OK ${VALIDATE_FILE});
  [[ $V == $synths ]] || { error=1; log "Speak error"; }

  echo $error
}

compare_channel()
{
  local same=1;
  local channel="";
  while read line;
  do 
    ch=$(echo $line | grep $@ | awk -F, '{print $5}')
    ch=${ch:0:16}
    if [[ ${#ch} != 0 ]]; then
      if [[ ${#channel} != 0 ]]; then
        #echo "Channel $channel"
        if [[ $channel != $ch ]]; then
          same=0
        fi
      else
       channel=$ch
      fi
    fi
     #echo "Ch      $ch"
  done < $CAPTURE_FILE;
  echo $same
}

validate-capture()
{
  log "Validate capture $@"
  local recognize=$6
  local rec_result=$7
  local verify=$8
  local ver_res=$9
  local ver_buf=${10}
  local rollback=${11}
  local clear=${12}
  local start=${13}
  local speak=${14}
  local same_chid=${15}
  local error=0

  sleep 1
  #log "Validate RECOGNIZE $recognize"
  V=$(validate RECOGNIZE $recognize $CAPTURE_FILE)
  [[ $V == 1 ]] && error=1
  #log "Validate RECOGNITION-COMPLETE $rec_result"
  V=$(validate RECOGNITION-COMPLETE $rec_result $CAPTURE_FILE)
  [[ $V == 1 ]] && error=1
  #log "Validate VERIFY $verify"
  V=$(validate VERIFY $verify $CAPTURE_FILE)
  [[ $V == 1 ]] && error=1
  #log "Validate VERIFICATION-COMPLETE $ver_res"
  V=$(validate VERIFICATION-COMPLETE $ver_res $CAPTURE_FILE)
  [[ $V == 1 ]] && error=1
  #log "Validate VERIFY-FROM-BUFFER $ver_buf"
  V=$(validate VERIFY-FROM-BUFFER $ver_buf $CAPTURE_FILE)
  [[ $V == 1 ]] && error=1
  #log "Validate ROLLBACK $rollback"
  V=$(validate ROLLBACK $rollback $CAPTURE_FILE)
  [[ $V == 1 ]] && error=1
  #log "Validate CLEAR-BUFFER $clear"
  V=$(validate CLEAR-BUFFER $clear $CAPTURE_FILE)
  [[ $V == 1 ]] && error=1
  #log "Validate START-SESSION $start"
  V=$(validate START-SESSION $start $CAPTURE_FILE)
  [[ $V == 1 ]] && error=1
  #log "Validate SPEAK $speak"
  V=$(validate SPEAK $speak $CAPTURE_FILE)
  [[ $V == 1 ]] && error=1
  #log "Validate SPEAK-COMPLETE $speak"
  V=$(validate SPEAK-COMPLETE $speak $CAPTURE_FILE)
  [[ $V == 1 ]] && error=1
  #log "Validate capture error=$error"
  local same=$(compare_channel -e recog -e verify)
  log "Same chid: $same"
  [[ $same_chid != $same ]] && error=1
  echo $error
}

execute-test()
{
  local extension=$1
  local audio=$2
  local RECOGS=$3
  local RESULTS=$4
  local SYNTHS=$5
  local test_result="success"
  local cap_result=""

  log "Executing test $extension"
  docker exec ${ASTERISK_CONTAINER} bash -c ">${RESULT_FILE};echo '' >${RESULT_FILE}"
  sudo rm $CAPTURE_FILE
  docker exec ${ASTERISK_CONTAINER} tcpdump -i any -w /tmp/cap.pcap &
  
  run-test $audio $extension

  docker exec ${ASTERISK_CONTAINER} pkill tcpdump
  docker cp ${ASTERISK_CONTAINER}:${RESULT_FILE} ${VALIDATE_FILE}
  docker cp ${ASTERISK_CONTAINER}:/tmp/cap.pcap /tmp
  sudo tshark -i any -r /tmp/cap.pcap -t r -d tcp.port==1544,mrcpv2 -T fields -E separator="," \
              -e frame.number -e frame.time_relative -e ip.src -e ip.dst -e mrcpv2.Channel-Identifier \
              -e _ws.col.Info -Y mrcpv2 >  $CAPTURE_FILE
  local err=$(validate-result $RECOGS $RESULTS $SYNTHS)
  [[ $err == 1 ]] && { FINAL_RESULT=1; test_result="failed"; }
  local err2=$(validate-capture $@)
  [[ $err2 == 1 ]] && { FINAL_RESULT=1; cap_result=" Capture failed."; }

  log "Test $extension $test_result.$cap_result"
  log "----------------------------------------"
  echo $err
}

tests=(
# dial                     Results                        Message capture
#                         __________________     ______________________________________________
# ext#  audio          recogs verifies synth recog rec-res ver ver-res buf rollback clear start speak same_chid
  301 dois-audios.wav    1       1       0     1      1     0     1     1      0      0     1     0     1
  302 dois-audios.wav    2       1       0     2      2     0     1     1      0      0     1     0     1
  303 dois-audios-2.wav  2       1       0     2      2     0     1     1      0      1     1     0     1
  304 dois-audios.wav    2       1       0     2      2     0     1     1      1      0     1     0     1
  305 dois-audios.wav    2       1       0     2      2     1     1     0      0      1     1     0     1
  306 tres-audios.wav    3       1       0     3      3     0     1     1      2      0     1     0     1
  307 um-audio.wav       1       0       0     1      1     0     0     0      0      0     0     0     1
  308 dois-audios.wav    2       0       0     2      2     0     0     0      0      0     0     0     1
  309 um-audio.wav       0       1       0     0      0     1     1     0      0      0     1     0     1
  310 dois-audios.wav    0       2       0     0      0     2     2     0      0      0     1     0     1
  311 um-audio.wav       1       1       0     1      1     0     1     1      0      0     1     0     1
  312 um-audio.wav       0       0       1     0      0     0     0     0      0      0     0     1     1
  313 um-audio.wav       1       0       1     1      1     0     0     0      0      0     0     1     1
  314 um-audio.wav       1       0       1     1      1     0     0     0      0      0     0     1     1
  315 um-audio.wav       1       1       1     1      1     0     1     1      0      0     1     1     1
  316 um-audio.wav       1       1       1     1      1     1     1     0      0      0     1     1     0
  317 dois-audios.wav    1       1       0     1      1     1     1     0      0      0     1     0     0
  318 dois-audios.wav    0       2       0     0      0     2     2     0      0      0     2     0     0
)
test_count=${#tests[*]}
i=0
[[ $1 == "all" ]] && all=1 || all=0

while [[ $i -lt $test_count ]]
do
  ext=${tests[$i]}; let i++
  audio=${tests[$i]}; let i++
  recogs=${tests[$i]}; let i++
  verifies=${tests[$i]}; let i++
  synths=${tests[$i]}; let i++
  recog_pkt=${tests[$i]}; let i++
  rec_res_pkt=${tests[$i]}; let i++
  ver_pkt=${tests[$i]}; let i++
  ver_res_pkt=${tests[$i]}; let i++
  ver_buf_pkt=${tests[$i]}; let i++
  rollback_pkt=${tests[$i]}; let i++
  clear_pkt=${tests[$i]}; let i++
  start_pkt=${tests[$i]}; let i++
  speak_pkt=${tests[$i]}; let i++
  same_chid=${tests[$i]}; let i++

  for par in $@
  do
    if [[ $all = 1 || $par = $ext ]]; then
      echo "Executing test: execute-test $ext $audio $recogs $verifies $synths $recog_pkt $rec_res_pkt $ver_pkt $ver_res_pkt $ver_buf_pkt $rollback_pkt $clear_pkt $start_pkt $speak_pkt $same_chid"
      execute-test $ext $audio $recogs $verifies $synths $recog_pkt $rec_res_pkt $ver_pkt $ver_res_pkt $ver_buf_pkt $rollback_pkt $clear_pkt $start_pkt $speak_pkt $same_chid
      continue
    fi
  done
done

cat $TEST_LOG
echo

[[ $FINAL_RESULT == 0 ]] && echo "Success" || echo "Failed"
[[ $FINAL_RESULT == 0 ]] && log "Test Success" || log "Test Failed"

exit $FINAL_RESULT
