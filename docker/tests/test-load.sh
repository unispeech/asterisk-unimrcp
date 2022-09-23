#!/bin/bash

n=0
END=100
RESULT_FILE="/tmp/test.txt"
ASTERISK_CONTAINER="asterisk-runner"

# test recog, verify
EXT=902
REC_RES=8
VER_RES=8
AUDIO1="audios/um-audio.wav"
AUDIO2="audios/pizza_pedra_audio_16k.wav"

# test 2xrecog, verify
#EXT=302
#REC_RES=16
#VER_RES=8
#AUDIO1="audios/dois-audios.wav"
#AUDIO2="audios/dois-audios.wav"

>/tmp/err

while [[ $n -lt $END ]]; do {
  docker exec ${ASTERISK_CONTAINER} bash -c ">${RESULT_FILE};echo '' >${RESULT_FILE}"
  n=$(($n+1));

  python3 sip-caller.py -f $AUDIO1 -t sip:${EXT}@${ASTERISK_CONTAINER} -c 6002@${ASTERISK_CONTAINER} &
  python3 sip-caller.py -f $AUDIO2 -t sip:${EXT}@${ASTERISK_CONTAINER} -c 6003@${ASTERISK_CONTAINER} &
  python3 sip-caller.py -f $AUDIO1 -t sip:${EXT}@${ASTERISK_CONTAINER} -c 6004@${ASTERISK_CONTAINER} &
  python3 sip-caller.py -f $AUDIO2 -t sip:${EXT}@${ASTERISK_CONTAINER} -c 6005@${ASTERISK_CONTAINER} &
  python3 sip-caller.py -f $AUDIO1 -t sip:${EXT}@${ASTERISK_CONTAINER} -c 6002@${ASTERISK_CONTAINER} &
  python3 sip-caller.py -f $AUDIO2 -t sip:${EXT}@${ASTERISK_CONTAINER} -c 6003@${ASTERISK_CONTAINER} &
  python3 sip-caller.py -f $AUDIO1 -t sip:${EXT}@${ASTERISK_CONTAINER} -c 6004@${ASTERISK_CONTAINER} &
  python3 sip-caller.py -f $AUDIO2 -t sip:${EXT}@${ASTERISK_CONTAINER} -c 6005@${ASTERISK_CONTAINER} && sleep 2

  docker cp ${ASTERISK_CONTAINER}:${RESULT_FILE} /tmp

  V=$(grep -c interpretation $RESULT_FILE);
  [[ $V == $REC_RES ]] && { echo "OK ===> $n" >> /tmp/err; } || { echo "RECOG NOK = $n" >> /tmp/err; };
  V=$(grep -c VERIF_RESULT $RESULT_FILE);
  [[ $V == $VER_RES ]] && { echo "OK ===> $n" >> /tmp/err; } || { echo "VERIF NOK = $n" >> /tmp/err; break; };
  V=$(grep -c johnsmith $RESULT_FILE); \
  [[ $V == $VER_RES ]] && { echo "OK ===> $n" >> /tmp/err; } || { echo "VOICEPRINT NOK = $n" >> /tmp/err; break; };

} done

cat /tmp/err
