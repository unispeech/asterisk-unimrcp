#!/bin/bash

n=0
END=100
EXT=902
REC_RES=8
VER_RES=8
#EXT=302
#REC_RES=16
#VER_RES=8
RESULT_FILE="/tmp/test.txt"
AUDIO1="audios/um-audio.wav"
AUDIO2="audios/pizza_pedra_audio_16k.wav"
#AUDIO1="audios/dois-audios.wav"
#AUDIO2="audios/dois-audios.wav"

while [[ $n -lt $END ]]; do {
  #sudo rm  /tmp/test.txt;
  docker exec -it asterisk bash -c ">${RESULT_FILE};echo '' >${RESULT_FILE}"
  n=$(($n+1));
  
  python3 sample.py -f $AUDIO1 -t sip:${EXT}@172.19.0.4 -c 6002@172.19.0.4 &
  python3 sample.py -f $AUDIO2 -t sip:${EXT}@172.19.0.4 -c 6003@172.19.0.4 &
  python3 sample.py -f $AUDIO1 -t sip:${EXT}@172.19.0.4 -c 6004@172.19.0.4 &
  python3 sample.py -f $AUDIO2 -t sip:${EXT}@172.19.0.4 -c 6005@172.19.0.4 &
  python3 sample.py -f $AUDIO1 -t sip:${EXT}@172.19.0.4 -c 6002@172.19.0.4 &
  python3 sample.py -f $AUDIO2 -t sip:${EXT}@172.19.0.4 -c 6003@172.19.0.4 &
  python3 sample.py -f $AUDIO1 -t sip:${EXT}@172.19.0.4 -c 6004@172.19.0.4 &
  python3 sample.py -f $AUDIO2 -t sip:${EXT}@172.19.0.4 -c 6005@172.19.0.4 && sleep 2

  V=$(grep -c interpretation $RESULT_FILE);
  [[ $V == $REC_RES ]] && { echo "OK ===> $n" >> /tmp/err; } || { echo "RECOG NOK = $n" >> /tmp/err; };
  V=$(grep -c VERIF_RESULT $RESULT_FILE);
  [[ $V == $VER_RES ]] && { echo "OK ===> $n" >> /tmp/err; } || { echo "VERIF NOK = $n" >> /tmp/err; break; };
  V=$(grep -c johnsmith $RESULT_FILE); \
  [[ $V == $VER_RES ]] && { echo "OK ===> $n" >> /tmp/err; } || { echo "VOICEPRINT NOK = $n" >> /tmp/err; break; };

} done
