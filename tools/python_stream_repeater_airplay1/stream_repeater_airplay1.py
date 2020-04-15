#!/usr/bin/env python3

import os
import sys

import http.client
import plistlib
import struct

class ScreenCastAirPlay1:
    def __init__(self, args):
        # Default AirPlay1 mirroring port: 7100
        self._address = (args[0], int(args[1]))

        self._client = None

    def _client_init(self):
        if not self._client:
            self._client = http.client.HTTPConnection('%s:%d' % self._address)

    def _request_stream_xml(self):
        headers = {
            'X-Apple-Device-ID': '0x7B:DE:DB:1F:BB:AB',
            'X-Apple-Client-Name': 'SM-G973F',
            'X-Apple-ProtocolVersion': '1',
            'rmodel': 'PC1,1',
        }
        self._client.request('GET', '/stream.xml', None, headers)
        resp = self._client.getresponse()
        print(resp)

    def _stream_start(self):
        plist = {
            'deviceID': '7B:DE:DB:1F:BB:AB',
            'latencyMs': 50,
            'sessionID': 84134,
            'version': '150.33',
            'fpsInfo': [
                { 'name': 'SubS' },
                { 'name': 'B4En' },
                { 'name': 'EnDp' },
                { 'name': 'IdEn' },
                { 'name': 'IdDp' },
                { 'name': 'EQDp' },
                { 'name': 'QueF' },
                { 'name': 'Sent' },
            ],
            'timestampInfo': [
                { 'name': 'SubSu' },
                { 'name': 'BePxT' },
                { 'name': 'AfPxt' },
                { 'name': 'BefEn' },
                { 'name': 'EmEnc' },
                { 'name': 'QueFr' },
                { 'name': 'SndFr' },
            ],
        }
        headers = {
            'X-Apple-Device-ID': '0x7B:DE:DB:1F:BB:AB',
            'X-Apple-Client-Name': 'SM-G973F',
            'X-Apple-ProtocolVersion': '1',
            'Content-Type': 'application/x-apple-binary-plist',
        }
        self._client.request('POST', '/stream', plistlib.dumps(plist, fmt=plistlib.FMT_BINARY), headers)

        # Send heartbeat header
        stream_header = struct.pack('<IHH', 0, 2, 0x1e).ljust(128, b'\x00')
        self._client.send(stream_header)

        # Send codec header
        #payload = 33
        #stream_header = struct.pack('<IHH', len(payload), 1, 6).ljust(128, b'\x09')
        #stream_header = b'\x21\x00\x00\x00\x01\x00\x1e\x00'
        #self._client.send(stream_header)

        import time
        for i in range(3):
            print('send header %d' % i)
            with open('data/header_%d.raw' % i, 'rb') as f:
                self._client.send(f.read())
        while True:
            print('Send frame')
            for i in range(3, 10):
                with open('data/frame_%d.raw' % i, 'rb') as f:
                    self._client.send(f.read())
            time.sleep(0.03333333333333333) # Wait to provide 30fps


    def process(self):
        self._client_init()
        self._request_stream_xml()
        self._stream_start()

def main():
    cv = ScreenCastAirPlay1(sys.argv[1:])
    cv.process()

if __name__ == '__main__':
    main()
