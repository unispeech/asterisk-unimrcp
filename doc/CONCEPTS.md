# UniMRCP Concepts

## General

The UniMRCP Asterisk applications control the access to media service resources such as speech synthesizers,
recognizers, verifiers, and identifiers from Asterisk dial plan using typical Asterisk Applications.

The Applications connect the caller extension to the resources using a MRCP profile, indicated in the Application options.
The profile can be associated to a MRCP Server using version 1 or 2. The MRCP profiles are defined in file mrcp.conf located
at Asterisk configuration folder [MRCP Profiles description](https://www.unimrcp.org/manuals/html/AsteriskUsageManual.html#_Toc485813115).

## Usage

The application usage follows the below syntax, wher arguments are mandatory and options are not mandatory:

```
ApplicationName(arguments[, options])

```

The application names are:

- MRCPSynth: establishes an MRCP session for speech synthesis.
- MRCPRecog: establishes an MRCP session for speech recognition.
- MRCPVerif: establishes an MRCP session for speaker verification.
- SynthAndRecog: establishes two MRCP sessions, one for speech synthesis and the other for speech recognition.
- MRCPRecogVerif: establishes an MRCP session for speech recognition, after recognition perform a speaker verification from audio buffer.

The detailed API description is available in: [API description](API.md)

## Resource Results

The result of the procedure with the result will be available to dial plan in channel context variables.
The variable names are listed below:

1. **MRCPSynth**:
   - SYNTHSTATUS: when synthesis completed is OK, otherwise, an error occurred (ERROR or INTERRUPTED by caller hang up.
   - SYNTH_COMPLETION_CAUSE: "000" - normal, "001" - barge-in, "002" - parse-failure ... see: [Synthesis completion cause](https://www.rfc-editor.org/rfc/rfc6787.html#section-8.4.4)

2. **MRCPRecog**:
   - RECOGSTATUS: when recognition was completed is OK, otherwise, an error occurred ERROR.
   - RECOG_COMPLETION_CAUSE: "000" - success, "001" - nomatch, "002" - noinput ... see: [Recogniton completion cause](https://www.rfc-editor.org/rfc/rfc6787.html#section-9.4.11)
   - RECOG_RESULT: contains the result in XML format

3. **MRCPVerif**:
   - VERIFSTATUS: when verification was completed is OK, otherwise, an error occurred ERROR.
   - VERIF_COMPLETION_CAUSE: : "000" - success, "001" - error, "002" - noinput ... see: [Verification completion cause](https://www.rfc-editor.org/rfc/rfc6787.html#section-11.4.16)
   - VERIF_RESULT: contains the result in XML format

4. **SynthAndRecog**: same results for *MRCPSynth* and *MRCPRecog*

5. **MRCPRecogVerif**: same results for *MRCPRecog* and *MRCPVerif*

## Session persistence

The Recognition and Verification Applications could share the audio buffer to perform a Recognition followed by Verification.
In order to achieve the buffer share, a single SIP session should be used to allocate an unique Channel Id for each resources
added to the session.

The below figure shows the SIP/MRCP Version 2 messages to achieve it:

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

For scenarios when a intermediate Recognition result is needed to validate a spoken password for instance, the applications
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

---

**Note**
- It is not possible to allocate in a same SIP session the three available resources: Synthesis, Recognition and Verification.
- The option *dse* (Datastore entry) could be used to diferentiate SIP sessions, otherwise Channel Id will be used.
- It is recommended the usage of Synthesis resource in a separated SIP session (without persistence), once it isn't need to share audio buffer for speech synthesis.
- The session persistence and buffer hanldling should be used with MRCP Version 2

---
