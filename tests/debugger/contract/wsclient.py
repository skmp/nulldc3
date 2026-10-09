"""A small client of the debugger server for the tests of this directory: one WebSocket connection (RFC 6455,
text messages), JSON-RPC requests over it, and the ticks that come in between.

It needs nothing but the Python library, so that a test which only has to add a breakpoint and to look at a
tick does not need the devtools checkout. The contract itself is held by the runner of the devtools UI.
"""

import base64
import json
import os
import socket
import struct
import time


class Error(Exception):
    pass


class Client:
    def __init__(self, host, port, timeout=10.0):
        self.next_id = 1
        self.pending = []       # ticks that came in and that nobody has looked at
        self.buffer = b''
        self.sock = socket.create_connection((host, port), timeout=timeout)

        key = base64.b64encode(os.urandom(16)).decode('ascii')
        request = ('GET /ws HTTP/1.1\r\nHost: %s:%d\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n'
                   'Sec-WebSocket-Key: %s\r\nSec-WebSocket-Version: 13\r\n\r\n' % (host, port, key))
        self.sock.sendall(request.encode('ascii'))

        deadline = time.monotonic() + timeout
        while b'\r\n\r\n' not in self.buffer:
            self.buffer += self._recv(deadline)
        head, self.buffer = self.buffer.split(b'\r\n\r\n', 1)
        status = head.split(b'\r\n', 1)[0].decode('latin-1')
        if ' 101 ' not in status + ' ':
            raise Error('the upgrade was answered "%s"' % status)

    def _recv(self, deadline):
        left = deadline - time.monotonic()
        if left <= 0:
            raise Error('timeout')
        self.sock.settimeout(left)
        try:
            data = self.sock.recv(65536)
        except socket.timeout:
            raise Error('timeout')
        if not data:
            raise Error('the server closed the connection')
        return data

    def _take(self, n, deadline):
        while len(self.buffer) < n:
            self.buffer += self._recv(deadline)
        data, self.buffer = self.buffer[:n], self.buffer[n:]
        return data

    def _frame(self, deadline):
        first, second = self._take(2, deadline)
        length = second & 0x7F
        if length == 126:
            length = struct.unpack('>H', self._take(2, deadline))[0]
        elif length == 127:
            length = struct.unpack('>Q', self._take(8, deadline))[0]
        if second & 0x80:
            raise Error('a frame of the server is masked')
        return first & 0x80 != 0, first & 0x0F, self._take(length, deadline)

    def _send(self, opcode, payload):
        mask = os.urandom(4)
        head = bytes([0x80 | opcode])
        if len(payload) < 126:
            head += bytes([0x80 | len(payload)])
        elif len(payload) < 65536:
            head += bytes([0x80 | 126]) + struct.pack('>H', len(payload))
        else:
            head += bytes([0x80 | 127]) + struct.pack('>Q', len(payload))
        masked = bytes(b ^ mask[i & 3] for i, b in enumerate(payload))
        self.sock.sendall(head + mask + masked)

    def _message(self, deadline):
        """The next text message, as the object its JSON is."""
        parts = []
        while True:
            fin, opcode, payload = self._frame(deadline)
            if opcode == 9:
                self._send(10, payload)
                continue
            if opcode == 10:
                continue
            if opcode == 8:
                code = struct.unpack('>H', payload[:2])[0] if len(payload) >= 2 else None
                raise Error('the server closed the connection (code %s)' % code)
            if opcode == 2:
                # binary: nothing of milestone M1 sends one
                if fin:
                    continue
            parts.append(payload)
            if fin:
                return json.loads(b''.join(parts).decode('utf-8'))

    def _next(self, deadline):
        """Reads one message. A tick goes to the pending ones; anything else is returned."""
        message = self._message(deadline)
        if message.get('method') == 'event.tick':
            self.pending.append(message['params'])
            return None
        return message

    def call(self, method, params, timeout=15.0):
        """A request and its result. A failure on either channel raises."""
        request_id = self.next_id
        self.next_id += 1
        self._send(1, json.dumps({'jsonrpc': '2.0', 'id': request_id, 'method': method, 'params': params}).encode('utf-8'))
        deadline = time.monotonic() + timeout
        while True:
            try:
                message = self._next(deadline)
            except Error as error:
                raise Error('%s: %s' % (method, error))
            if message is None or message.get('id') != request_id:
                continue
            if 'error' in message:
                raise Error('%s: error %s' % (method, message['error']))
            result = message['result']
            if isinstance(result, dict) and 'error' in result:
                raise Error('%s: error %s' % (method, result['error']))
            return result

    def describe(self, timeout=15.0):
        """debugger.describe; returns the full tick that follows its response."""
        self.shape = self.call('debugger.describe', {'protocol': 3, 'client': 'tests/debugger/contract'}, timeout)
        # the tick of the describe is the first one behind its response
        self.pending = []
        return self.wait_tick(lambda tick: True, timeout, 'the tick behind debugger.describe')

    def wait_tick(self, wanted, timeout, what):
        """The first tick, of those nobody has looked at and those that come, that `wanted` takes."""
        deadline = time.monotonic() + timeout
        while True:
            while self.pending:
                tick = self.pending.pop(0)
                if wanted(tick):
                    return tick
            try:
                self._next(deadline)
            except Error as error:
                raise Error('waiting for %s: %s' % (what, error))

    def close(self):
        try:
            self._send(8, struct.pack('>H', 1000))
            # the server answers the close and ends the connection
            self.sock.settimeout(3.0)
            while self.sock.recv(65536):
                pass
        except (OSError, Error):
            pass
        self.sock.close()
