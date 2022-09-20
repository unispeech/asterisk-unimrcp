# Asterisk UniMRCP API documentation

## Summary

The Asterisk UniMRCP API implements callable applications from Asterisk dial plans to execute calls for MRCP resources:
- Speech Synthesis
- Speech Recognition
- Speaker Verification.

## Synthesis Application

```
[Synopsis]
MRCP synthesis application. 

[Description]
This application establishes an MRCP session for speech synthesis.
If synthesis completed, the variable ${SYNTHSTATUS} is set to "OK"; otherwise,
if an error occurred,  the variable ${SYNTHSTATUS} is set to "ERROR". If the
caller hung up while the synthesis was in-progress,  the variable
${SYNTHSTATUS} is set to "INTERRUPTED".
The variable ${SYNTH_COMPLETION_CAUSE} indicates whether synthesis completed
normally or with an error. ("000" - normal, "001" - barge-in, "002" -
parse-failure, ...) 

[Syntax]
MRCPSynth(prompt[,options])

[Arguments]
prompt
    A prompt specified as a plain text, an SSML content, or by means of a file
    or URI reference.
options
    p: Profile to use in mrcp.conf.

    i: Digits to allow the TTS to be interrupted with.

    f: Filename on disk to store audio to (audio not stored if not specified or
    empty).

    l: Language to use (e.g. "en-GB", "en-US", "en-AU", etc.).

    ll: Load lexicon (true/false).

    pv: Prosody volume (silent/x-soft/soft/medium/loud/x-loud/default).

    pr: Prosody rate (x-slow/slow/medium/fast/x-fast/default).

    v: Voice name to use (e.g. "Daniel", "Karin", etc.).

    g: Voice gender to use (e.g. "male", "female").

    vv: Voice variant.

    a: Voice age.

    plt: Persistent lifetime (0: no [MRCP session is created and destroyed
    dynamically], 1: yes [MRCP session is created on demand, reused and
    destroyed on hang-up].

    dse: Datastore entry.

    sbs: Always stop barged synthesis request.

    vsp: Vendor-specific parameters.
```

## Recognition Application

```
[Synopsis]
MRCP recognition application. 

[Description]
This application establishes an MRCP session for speech recognition and
optionally plays a prompt file. Once recognition completes, the application
exits and returns results to the dialplan.
If recognition completed, the variable ${RECOGSTATUS} is set to "OK".
Otherwise, if recognition couldn't be started, the variable ${RECOGSTATUS} is
set to "ERROR". If the caller hung up while recognition was still in-progress,
the variable ${RECOGSTATUS} is set to "INTERRUPTED".
The variable ${RECOG_COMPLETION_CAUSE} indicates whether recognition completed
successfully with a match or an error occurred. ("000" - success, "001" -
nomatch, "002" - noinput) 
If recognition completed successfully, the variable ${RECOG_RESULT} is set to
an NLSML result received from the MRCP server. Alternatively, the recognition
result data can be retrieved by using the following dialplan functions
RECOG_CONFIDENCE(), RECOG_GRAMMAR(), RECOG_INPUT(), and RECOG_INSTANCE().

[Syntax]
MRCPRecog(grammar[,options])

[Arguments]
grammar
    An inline or URI grammar to be used for recognition.
options
    p: Profile to use in mrcp.conf.

    i: Digits to allow recognition to be interrupted with (set to "none" for
    DTMF grammars to allow DTMFs to be sent to the MRCP server; otherwise, if
    "any" or other digits specified, recognition will be interrupted and the
    digit will be returned to dialplan).

    f: Filename to play (if empty or not specified, no file is played).

    t: Recognition timeout (msec).

    b: Bargein value (0: no barge-in, 1: enable barge-in).

    gd: Grammar delimiters.

    ct: Confidence threshold (0.0 - 1.0).

    sl: Sensitivity level (0.0 - 1.0).

    sva: Speed vs accuracy (0.0 - 1.0).

    nb: N-best list length.

    nit: No input timeout (msec).

    sct: Speech complete timeout (msec).

    sint: Speech incomplete timeout (msec).

    dit: DTMF interdigit timeout (msec).

    dtt: DTMF terminate timeout (msec).

    dttc: DTMF terminate characters.

    sw: Save waveform (true/false).

    nac: New audio channel (true/false).

    spl: Speech language (e.g. "en-GB", "en-US", "en-AU", etc.).

    rm: Recognition mode (normal/hotword).

    hmaxd: Hotword max duration (msec).

    hmind: Hotword min duration (msec).

    cdb: Clear DTMF buffer (true/false).

    enm: Early nomatch (true/false).

    iwu: Input waveform URI.

    vbu: Verify Buffer Utterance (true/false).

    mt: Media type.

    epe: Exit on play error  (1: terminate recognition on file play error, 0:
    continue even if file play fails).

    uer: URI-encoded results  (1: URI-encode NLMSL results, 0: do not encode).

    od: Output (prompt) delimiters.

    sit: Start input timers value (0: no, 1: yes [start with RECOGNIZE],  2:
    auto [start when prompt is finished]).

    plt: Persistent lifetime (0: no [MRCP session is created and destroyed
    dynamically], 1: yes [MRCP session is created on demand, reused and
    destroyed on hang-up].

    dse: Datastore entry.

    vsp: Vendor-specific parameters.

    nif: NLSML instance format (either "xml" or "json") used by
    RECOG_INSTANCE().

```

## Synthesis and Recognition Application

```
[Synopsis]
Play a synthesized prompt and wait for speech to be recognized. 

[Description]
This application establishes two MRCP sessions: one for speech synthesis and
the other for speech recognition. Once the user starts speaking (barge-in
occurred), the synthesis session is stopped, and the recognition engine starts
processing the input. Once recognition completes, the application exits and
returns results to the dialplan.
If recognition completed, the variable ${RECOG_STATUS} is set to "OK".
Otherwise, if recognition couldn't be started, the variable ${RECOG_STATUS} is
set to "ERROR". If the caller hung up while recognition was still in-progress,
the variable ${RECOG_STATUS} is set to "INTERRUPTED".
The variable ${RECOG_COMPLETION_CAUSE} indicates whether recognition completed
successfully with a match or an error occurred. ("000" - success, "001" -
nomatch, "002" - noinput) 
If recognition completed successfully, the variable ${RECOG_RESULT} is set to
an NLSML result received from the MRCP server. Alternatively, the recognition
result data can be retrieved by using the following dialplan functions
RECOG_CONFIDENCE(), RECOG_GRAMMAR(), RECOG_INPUT(), and RECOG_INSTANCE().

[Syntax]
SynthAndRecog(prompt,grammar[,options])

[Arguments]
prompt
    A prompt specified as a plain text, an SSML content, or by means of a file
    or URI reference.
grammar
    An inline or URI grammar to be used for recognition.
options
    p: Profile to use in mrcp.conf.

    t: Recognition timeout (msec).

    b: Bargein value (0: no barge-in, 1: enable barge-in).

    gd: Grammar delimiters.

    ct: Confidence threshold (0.0 - 1.0).

    sl: Sensitivity level (0.0 - 1.0).

    sva: Speed vs accuracy (0.0 - 1.0).

    nb: N-best list length.

    nit: No input timeout (msec).

    sct: Speech complete timeout (msec).

    sint: Speech incomplete timeout (msec).

    dit: DTMF interdigit timeout (msec).

    dtt: DTMF terminate timeout (msec).

    dttc: DTMF terminate characters.

    sw: Save waveform (true/false).

    nac: New audio channel (true/false).

    spl: Speech language (en-US/en-GB/etc).

    rm: Recognition mode (normal/hotword).

    hmaxd: Hotword max duration (msec).

    hmind: Hotword min duration (msec).

    cdb: Clear DTMF buffer (true/false).

    enm: Early nomatch (true/false).

    iwu: Input waveform URI.

    vbu: Verify Buffer Utterance (true/false).

    mt: Media type.

    pv: Prosody volume (silent/x-soft/soft/medium/loud/x-loud/default).

    pr: Prosody rate (x-slow/slow/medium/fast/x-fast/default).

    vn: Voice name to use (e.g. "Daniel", "Karin", etc.).

    vg: Voice gender to use (e.g. "male", "female").

    vv: Voice variant.

    a: Voice age.

    uer: URI-encoded results  (1: URI-encode NLMSL results, 0: do not encode).

    od: Output (prompt) delimiters.

    sit: Start input timers value (0: no, 1: yes [start with RECOGNIZE], 2:
    auto [start when prompt is finished]).

    plt: Persistent lifetime (0: no [MRCP session is created and destroyed
    dynamically], 1: yes [MRCP session is created on demand, reused and
    destroyed on hang-up].

    dse: Datastore entry.

    sbs: Always stop barged synthesis request.

    vsp: Vendor-specific parameters.

    nif: NLSML instance format (either "xml" or "json") used by
    RECOG_INSTANCE().
```

## Verification Application

```
[Synopsis]
MRCP verification application. 

[Description]
This application establishes an MRCP session for speak verification and
optionally plays a prompt file. Once recognition completes, the application
exits and returns results to the dialplan.
If recognition completed, the variable ${VERIFSTATUS} is set to "OK".
Otherwise, if recognition couldn't be started, the variable ${VERIFSTATUS} is
set to "ERROR". If the caller hung up while recognition was still in-progress,
the variable ${VERIFSTATUS} is set to "INTERRUPTED".
The variable ${RECOG_COMPLETION_CAUSE} indicates whether recognition completed
successfully with a match or an error occurred. ("000" - success, "001" -
nomatch, "002" - noinput) 
If recognition completed successfully, the variable ${VERIF_RESULT} is set to
an NLSML result received from the MRCP server. Alternatively, the recognition
result data can be retrieved by using the following dialplan functions
RECOG_CONFIDENCE(), RECOG_GRAMMAR(), RECOG_INPUT(), and RECOG_INSTANCE().

[Syntax]
MRCPVerif([options])

[Arguments]
options
    p: Profile to use in mrcp.conf.

    i: Digits to allow recognition to be interrupted with (set to "none" for
    DTMF grammars to allow DTMFs to be sent to the MRCP server; otherwise, if
    "any" or other digits specified, recognition will be interrupted and the
    digit will be returned to dialplan).

    f: Filename to play (if empty or not specified, no file is played).

    b: Bargein value (0: no barge-in, 1: enable barge-in).

    vc: Verificarion score (-1.0 - 1.0).

    minph: Minimum verification phrases.

    maxph: Maximum verification phrases.

    nit: No input timeout (msec).

    sct: Speech complete timeout (msec).

    dit: DTMF interdigit timeout (msec).

    dtt: DTMF terminate timeout (msec).

    dttc: DTMF terminate characters.

    sw: Save waveform (true/false).

    vm: Verification mode (verify/enroll).

    enm: Early nomatch (true/false).

    iwu: Input waveform URI.

    rpuri: Repository URI.

    vpid: Voiceprint identifier.

    mt: Media type.

    iwu: Input waveform URI.

    vbu: Verify Buffer Utterance (true/false).

    bufh:  Control buffer handling ( verify: Perform a verify from audio
    buffer, clear: Perform a buffer clear and rollback: Perform a buffer
    rollback).

    uer: URI-encoded results  (1: URI-encode NLMSL results, 0: do not encode).

    sit: Start input timers value (0: no, 1: yes [start with RECOGNIZE],  2:
    auto [start when prompt is finished]).

    vsp: Vendor-specific parameters.

    nif: NLSML instance format (either "xml" or "json") used by
    RECOG_INSTANCE().

```

## Recognition and Verification Application

```
[Synopsis]
MRCP recognition application. 

[Description]
This application establishes an MRCP session for speech recognition and
optionally plays a prompt file. Once recognition completes, the application
exits and returns results to the dialplan.
If recognition completed, the variable ${RECOG_VERIF_STATUS} is set to "OK".
Otherwise, if recognition couldn't be started, the variable
${RECOG_VERIF_STATUS} is set to "ERROR". If the caller hung up while
recognition was still in-progress, the variable ${RECOG_VERIF_STATUS} is set to
"INTERRUPTED".
The variable ${RECOG_COMPLETION_CAUSE} indicates whether recognition completed
successfully with a match or an error occurred. ("000" - success, "001" -
nomatch, "002" - noinput) 
If recognition completed successfully, the variable ${RECOG_RESULT} is set to
an NLSML result received from the MRCP server. Alternatively, the recognition
result data can be retrieved by using the following dialplan functions
RECOG_CONFIDENCE(), RECOG_GRAMMAR(), RECOG_INPUT(), and RECOG_INSTANCE().

[Syntax]
MRCPRecogVerif(grammar[,options])

[Arguments]
grammar
    An inline or URI grammar to be used for recognition.
options
    p: Profile to use in mrcp.conf.

    i: Digits to allow recognition to be interrupted with (set to "none" for
    DTMF grammars to allow DTMFs to be sent to the MRCP server; otherwise, if
    "any" or other digits specified, recognition will be interrupted and the
    digit will be returned to dialplan).

    f: Filename to play (if empty or not specified, no file is played).

    t: Recognition timeout (msec).

    b: Bargein value (0: no barge-in, 1: enable barge-in).

    vc: Verificarion score (-1.0 - 1.0).

    minph: Minimum verification phrases.

    maxph: Maximum verification phrases.

    gd: Grammar delimiters.

    ct: Confidence threshold (0.0 - 1.0).

    sl: Sensitivity level (0.0 - 1.0).

    sva: Speed vs accuracy (0.0 - 1.0).

    nb: N-best list length.

    nit: No input timeout (msec).

    sct: Speech complete timeout (msec).

    sint: Speech incomplete timeout (msec).

    dit: DTMF interdigit timeout (msec).

    dtt: DTMF terminate timeout (msec).

    dttc: DTMF terminate characters.

    sw: Save waveform (true/false).

    vm: Verification mode (verify/enroll).

    nac: New audio channel (true/false).

    spl: Speech language (e.g. "en-GB", "en-US", "en-AU", etc.).

    rm: Recognition mode (normal/hotword).

    hmaxd: Hotword max duration (msec).

    hmind: Hotword min duration (msec).

    cdb: Clear DTMF buffer (true/false).

    enm: Early nomatch (true/false).

    iwu: Input waveform URI.

    rpuri: Repository URI.

    vpid: Voiceprint identifier.

    mt: Media type.

    vbu: Verify Buffer Utterance (true/false).

    epe: Exit on play error  (1: terminate recognition on file play error, 0:
    continue even if file play fails).

    uer: URI-encoded results  (1: URI-encode NLMSL results, 0: do not encode).

    od: Output (prompt) delimiters.

    sit: Start input timers value (0: no, 1: yes [start with RECOGNIZE],  2:
    auto [start when prompt is finished]).

    plt: Persistent lifetime (0: no [MRCP session is created and destroyed
    dynamically], 1: yes [MRCP session is created on demand, reused and
    destroyed on hang-up].

    dse: Datastore entry.

    vsp: Vendor-specific parameters.

    vsprec: Vendor-specific parameters for recognition.

    vspver: Vendor-specific parameters for verify.

    nif: NLSML instance format (either "xml" or "json") used by
    RECOG_INSTANCE().
```

## Session persistence

The Recognition and Verification Applications could share the audio buffer to perform a Recognition followed by Verification,
to achieve the buffer share a single SIP session should be used to allocate an unique Channel Id for the resources add to session.

The below figure shows the SIP/MRCP message to achieve it

```
       MRCP Client                         MRCP
        (Asterisk)                        Server
            |                               |
            | SIP Invite (speechrecog)      |
            |------------------------------>|
            |                    SIP Trying |
            |<------------------------------|
            |                               |
            |      SIP OK (SDP: Channel-Id) |
            |<------------------------------|
            | MRCP RECOGNIZE (w/ Ch.Id)     |
            |------------------------------>|
            |                               |
            |                   IN PROGRESS |
            |<------------------------------|
            | RTP FLOW                      |
            |------------------------------>|
            |                               |
            | (...)                         |
            |                               |
            |                               |
            |                               |
            |          RECOGNITION COMPLETE |
            |<------------------------------|
            |                               |
            | SIP Invite (speakverify)      | Note: The speakverify resource
            |------------------------------>|       is add to call with same
            |                    SIP Trying |       Channel-Id
            |<------------------------------|
            |                               |
            |      SIP OK (SDP: Channel-Id) |
            |<------------------------------|
            | MRCP START-SESSION (w/ Ch.Id) |
            |------------------------------>|
            |                               |
            |<---------------------COMPLETE-|
            | MRCP VERIFY-FROM-BUFFER       |
            |------------------------------>|
            |                               |
            |                   IN PROGRESS |
            |<------------------------------|
            |                               |
            |         VERIFICATION COMPLETE |
            |<------------------------------|
            | MRCP END-SESSION              |
            |------------------------------>|
            |                               |
            |<---------------------COMPLETE-|

```

The Aplication *MRCPRecogVerif* already implements such sequence, but the result for Recognition and Verification is just
available at the end of the application processing.

For scenarios when a intermediate Recognition result is needed to validate a spoken password foi instance, the applications
*MRCPRecog* and *MRCPVerif* could be called in sequence with session persistence. In order to control the persistence,
to guarantee that Recogniton and Verification resources will use the same Channel-Id, the *plt* parameter
must be used to keep the session between to consecutives called MRCP Applications in dialplan. Besides the parameter *vbu*
is needed in the *MRCPRecog* call to keep the audio buffer and the parameter *bufh* should indicate a buffer use for
verification in *MRCPVerif* call, like show below:

```
exten => 301,1,Answer
same => n,MRCPRecog(builtin:slm/general, p=default&f=hello-world&sct=2000&vbu=true&plt=1)
same => n,Verbose(1, ${RECOGSTATUS}, ${RECOG_COMPLETION_CAUSE}, ${RECOG_RESULT})
same => n,MRCPVerif(vm=verify&rpuri=https://ocibio2.aquarius.cpqd.com.br:8665&vpid=johnsmith,marysmith&p=default&f=please-try-again&sct=2000&sit=1&plt=1&bufh=verify-from-buffer)
same => n,Verbose(2, ${VERIFSTATUS}, ${VERIF_COMPLETION_CAUSE}, ${VERIF_RESULT}), ${VERIF_SID})
same => n,Hangup
```

The session is only released when the hangup is executed.
