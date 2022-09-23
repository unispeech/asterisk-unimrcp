#!/usr/bin/python
import sys
import os
import time

from application.notification import NotificationCenter
from optparse import OptionParser
from sipsimple.account import AccountManager
from sipsimple.application import SIPApplication
from sipsimple.core import SIPURI, ToHeader
from sipsimple.audio import WavePlayer
from sipsimple.lookup import DNSLookup, DNSLookupError
from sipsimple.storage import FileStorage
from sipsimple.session import Session
from sipsimple.streams import MediaStreamRegistry
from sipsimple.threading.green import run_in_green_thread
from threading import Event

CALLER_ACCOUNT  = '6002@asterisk-runner'             # must be configured in config/config
TARGET_URI      = 'sip:901@asteris-runner'           # SIP URI we want to call

class SimpleCallApplication(SIPApplication):

    def __init__(self):
        SIPApplication.__init__(self)
        self.ended = Event()
        self.callee = None
        self.session = None
        self._wave_file = None
        notification_center = NotificationCenter()
        notification_center.add_observer(self)

    def call(self, callee, filetoplay, caller):
        print('Placing call to %s' % callee)
        self.callee = callee
        self._wave_file = filetoplay
        if caller.find("6002") >= 0 or caller.find("6004") >= 0:
            self.start(FileStorage('config'))
        else:
            self.start(FileStorage('config2'))

    @run_in_green_thread
    def _NH_SIPApplicationDidStart(self, notification):
        print('Callback: application started')
        self.callee = ToHeader(SIPURI.parse(self.callee))
        # Retrieve account from config
        try:
            account = AccountManager().get_account(CALLER_ACCOUNT)
            host = account.sip.outbound_proxy.host
            port = account.sip.outbound_proxy.port
            transport = account.sip.outbound_proxy.transport
            mixer = SIPApplication.voice_audio_mixer
            mixer.output_volume = 0
            mixer.muted = True
            print("Output: {}".format(mixer.output_device))
            #mixer.set_sound_devices("system_default", "/dev/audio")
            self.player = WavePlayer(mixer, self._wave_file, loop_count=0)
            print('      Host = %s\n      Port = %s\n Transport = %s' % (host, port, transport))
        except Exception as e:
            print('ERROR 1: %s' % e)
        try:
            uri = SIPURI(host=host, port=port, parameters={'transport': transport})
            routes = DNSLookup().lookup_sip_proxy(uri, ['tcp', 'udp']).wait()
        except DNSLookupError as e:
            print('ERROR: DNS lookup failed: %s' %  e)
        else:
            self.session = Session(account)
            print('Routes: %s' % routes)
            self.session.connect(self.callee, routes, streams=[MediaStreamRegistry.AudioStream()])

    def _NH_SIPSessionGotRingIndication(self, notification):
        print('Callback: ringing')

    def _NH_SIPSessionDidStart(self, notification):
        print('Callback: session started')
        try:
            audio_stream = notification.data.streams[0]
            print('Audio session established using "%s" codec at %sHz' % (audio_stream.codec, audio_stream.sample_rate))
            session = notification.sender
            audio_stream = session.streams[0]
            audio_stream.bridge.add(self.player)
            self.player.play()
        except:
            print(notification)

    def _NH_SIPSessionDidFail(self, notification):
        print('Callback: failed to connect')
        try:
            print(notification.data.code, notification.data.reason)
        except:
            print(notification)
        self.stop()

    def _NH_SIPSessionDidEnd(self, notification):
        print('Callback: session ended')
        time.sleep(10)
        self.stop()

    def _NH_SIPApplicationDidEnd(self, notification):
        print('Callback: application ended')
        time.sleep(5)
        self.ended.set()
    def _NH_WavePlayerDidFail(self, notifications):
        print('Callback: PLAYER failed!!!')

if __name__ == '__main__':
    parser = OptionParser()
    parser.add_option('-t', '--target', help='Target URI')
    parser.add_option('-c', '--caller', help='Caller Account')
    parser.add_option('-f', '--file', dest='filename', help='File to play', metavar='FILE')
    options, args = parser.parse_args()
    print(options)
    print(args)

    if not options.filename:
        print('Filename need to be specified, try --help')
        sys.exit(1)
    if not os.path.isfile(options.filename):
        print("The specified file doesn't exist")
        sys.exit(1)
    if not options.target:
        print('Target not specified, Using: %s' % TARGET_URI)
    else:
        TARGET_URI=options.target
    if not options.caller:
        print('Caller not specified, Using: %s' % CALLER_ACCOUNT)
    else:
        CALLER_ACCOUNT=options.caller
    # place an audio call to the specified SIP URI
    application = SimpleCallApplication()
    application.call(TARGET_URI, options.filename, CALLER_ACCOUNT)
    #raw_input('Press Enter to exit\n\n')
    try:
        if application.session != None:
            application.session.end()
        application.ended.wait()
    except Exception as e:
        print('ERROR Main: %s' % e)
        
