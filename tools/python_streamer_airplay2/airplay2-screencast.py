#!/usr/bin/env python3

import os
import sys

import hashlib
from cryptography.hazmat.backends import default_backend
from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey
from cryptography.hazmat.primitives.asymmetric.x25519 import (
    X25519PrivateKey,
    X25519PublicKey,
)

from lib.rtsp.control import RTSPClient

import plistlib
import socket
import struct

def hash_sha512(*indata):
    """Create SHA512 hash for input arguments."""
    hasher = hashlib.sha512()
    for data in indata:
        if isinstance(data, str):
            hasher.update(data.encode("utf-8"))
        elif isinstance(data, bytes):
            hasher.update(data)
        else:
            raise Exception("invalid input data: " + str(data))
    return hasher.digest()

def aes_encrypt(mode, aes_key, aes_iv, *data):
    """Encrypt data with AES in specified mode."""
    encryptor = Cipher(
        algorithms.AES(aes_key), mode(aes_iv), backend=default_backend()
    ).encryptor()

    result = None
    for value in data:
        result = encryptor.update(value)
    encryptor.finalize()

    return result, None if not hasattr(encryptor, "tag") else encryptor.tag

class ScreenCast:
    def __init__(self, args):
        self._address = (args[0], int(args[1]))
        self.seed = os.urandom(32)  # Generate new seed if not provided
        signing_key = Ed25519PrivateKey.from_private_bytes(self.seed)
        verifying_key = signing_key.public_key()
        self._auth_private = signing_key.private_bytes(
            encoding=serialization.Encoding.Raw,
            format=serialization.PrivateFormat.Raw,
            encryption_algorithm=serialization.NoEncryption(),
        )
        self._auth_public = verifying_key.public_bytes(
            encoding=serialization.Encoding.Raw, format=serialization.PublicFormat.Raw
        )

        self._client = None

    def _client_init(self):
        if not self._client:
            self._client = RTSPClient("rtsp://%s:%d/" % self._address)
            #self._client = RTSPClient("rtsp://172.17.0.2:5001/")
            #self._client = RTSPClient("rtsp://192.168.30.243:7001/")
        self._client.connect()

    def _pair_setup(self):
        """pair-setup"""
        res = self._client._request(
            method='POST',
            url='/pair-setup',
            headers={ # TODO: missed optional headers
                'Content-Type': 'application/octet-stream',
                'Content-Length': str(len(self._auth_public))
            },
            data=self._auth_public,
        )

        # TODO: verify 32 bytes in body
        print(len(res.body))
        self._ed_dsa_theirs = res.body

    def _pair_verify1(self):
        """pair-verify stage 1"""
        self._verify_private = X25519PrivateKey.from_private_bytes(self.seed)
        self._verify_public = self._verify_private.public_key()

        # Pair-setup 1
        self._public_bytes = self._verify_public.public_bytes(
            encoding=serialization.Encoding.Raw, format=serialization.PublicFormat.Raw
        )

        data = b"\x01\x00\x00\x00" + self._public_bytes + self._auth_public

        res = self._client._request(
            method='POST',
            url='/pair-verify',
            headers={ # TODO: missed optional headers
                'Content-Type': 'application/octet-stream',
                'Content-Length': str(len(data))
            },
            data=data,
        )

        # Get server ecdh
        # TODO: verify 96 bytes in body
        # TODO: verify server signature
        print(len(res.body))
        self._ec_pubkey_theirs = res.body[:32]
        self._signature_theirs = res.body[32:]

    def _pair_verify2(self):
        """pair-verify stage 2"""
        # Generate a shared secret key
        shared = self._verify_private.exchange(
            X25519PublicKey.from_public_bytes(self._ec_pubkey_theirs)
        )

        # Derive new AES key and IV from shared key
        aes_key = hash_sha512("Pair-Verify-AES-Key", shared)[0:16]
        aes_iv = hash_sha512("Pair-Verify-AES-IV", shared)[0:16]

        # Sign public keys and encrypt with AES
        signer = Ed25519PrivateKey.from_private_bytes(self._auth_private)
        signed = signer.sign(self._public_bytes + self._ec_pubkey_theirs)
        signature, _ = aes_encrypt(modes.CTR, aes_key, aes_iv, self._signature_theirs, signed)

        data = b'\x00\x00\x00\x00' + signature

        res = self._client._request(
            method='POST',
            url='/pair-verify',
            headers={ # TODO: missed optional headers
                'Content-Type': 'application/octet-stream',
                'Content-Length': str(len(data))
            },
            data=data,
        )

        print(res.headers)

    def _fp_setup1(self):
        """fp-setup"""

        data = b'FPLY'
        data += b'\x03' # FairPlay version
        data = data.ljust(14, b'\x00')
        data += b'\x03' # Mode
        data = data.ljust(16, b'\x00')
        print(len(data))

        res = self._client._request(
            method='POST',
            url='/fp-setup',
            headers={ # TODO: missed optional headers
                'Content-Type': 'application/octet-stream',
                'Content-Length': str(len(data))
            },
            data=data,
        )

        print(res.headers)
        print(len(res.body))
        # TODO: ??do something with the body??

    def _fp_setup2(self):
        """Implement the second stage of fp-setup"""
        # TODO: right now it's hard to implement this required stage

    def _rtsp_setup(self):
        """Setup RTSP connection"""
        shared = self._verify_private.exchange(
            X25519PublicKey.from_public_bytes(self._ec_pubkey_theirs)
        )

        # Derive new AES key and IV from shared key
        aes_key = hash_sha512("Pair-Verify-AES-Key", shared)[0:16]
        aes_iv = hash_sha512("Pair-Verify-AES-IV", shared)[0:16]

        plist = {
            "ekey": aes_key, # TODO: not encrypted
            "eiv": aes_iv,
            "streams":[{
                "type": 110,
                "streamConnectionID": 4964383553955644435,
            }],
        }
        data = plistlib.dumps(plist, fmt=plistlib.FMT_BINARY)

        res = self._client._request(
            method='SETUP',
            url=self._client.safe_url+'4882189185445544350',
            headers={ # TODO: missed optional headers
                'Content-Type': 'application/x-apple-binary-plist',
                'Content-Length': str(len(data))
            },
            data=data,
        )

        self._rtsp_setup_data = plistlib.loads(res.body)
        print(self._rtsp_setup_data)

    def _rtsp_teardown(self):
        """Close the stream"""

        plist = {
            "streams":[{
                "type": 110,
            }],
        }
        data = plistlib.dumps(plist, fmt=plistlib.FMT_BINARY)

        res = self._client._request(
            method='TEARDOWN',
            url=self._client.safe_url+'4882189185445544350',
            headers={ # TODO: missed optional headers
                'Content-Type': 'application/x-apple-binary-plist',
                'Content-Length': str(len(data))
            },
            data=data,
        )
        print(res.headers)

    def process(self):
        self._client_init()
        self._pair_setup()
        self._pair_verify1()
        self._pair_verify2()

        # TODO: Currently fp-setup is not properly implemented
        self._fp_setup1()
        self._fp_setup2()

        sys.exit(0) # TODO: until fp-setup is not working - we can't continue

        self._rtsp_setup()
        self._video_socket = socket.socket(socket.AF_INET,socket.SOCK_STREAM)
        self._video_socket.connect((self._address[0], self._rtsp_setup_data['streams'][0]['dataPort']))
        self._video_socket.setblocking(False)

        try:
            payload = b''
            header = struct.pack('<IHH', len(payload), 0, 0)

            # To send SPSPPS
            #header = struct.pack('<IHH', len(payload), 1, 0)
            #header = header.ljust(40, b'\x00') # Added filling till withSource/heightSource
            #header += struct.pack('<ff', 1920.0, 1080.0)
            #header = header.ljust(56, b'\x00') # Added filling till with/height
            #header += struct.pack('<ff', 1920.0, 1080.0)

            header += header.ljust(128, b'\x00')

            #self._video_socket.send(header + payload)
        finally:
            self._video_socket.close()
            # TODO: Enable teardown
            #self._rtsp_teardown()


def main():
    cv = ScreenCast(sys.argv[1:])
    cv.process()

if __name__ == '__main__':
    main()
