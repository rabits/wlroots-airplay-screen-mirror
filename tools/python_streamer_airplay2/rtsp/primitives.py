import time
from io import BytesIO

class RTSPResponse:
    def __init__(self, header_lines):
        self.version, self.status, self.headers = self._parse_header(header_lines)
        self.body = None

    def __repr__(self):
        return f'<RTSPResponse [{self.status}]>'

    @staticmethod
    def _parse_header(header_lines):
        version, status = header_lines[0].split(None,2)[:2]

        headers = dict()
        for l in header_lines[1:]:
            if l.strip():
                print(l)
                k, v = l.split(':', 1)
                if k not in headers:
                    headers[k.lower()] = v.strip()

        return version, int(status), headers


class RTSPRequest:
    VERSION = 'RTSP/1.0'
    LINE_SPLIT = '\r\n'
    HEADER_END = LINE_SPLIT * 2

    def __init__(self, socket, method, url, headers={}, data=None):
        self.socket = socket
        self.method = method.upper()
        self.url = url
        self.headers = headers
        self.data = data

    def __repr__(self):
        return f'<RTSPRequest [{self.method}]>'

    def _prepare_headers(self):
        prep = str()
        for k, v in self.headers.items():
            prep += f'{self.LINE_SPLIT}{k}: {v}'

        return prep

    def send(self):
        msg = f'{self.method} {self.url} {self.VERSION}'
        msg += self._prepare_headers()
        msg += self.HEADER_END

        self.socket.send(bytes(msg, 'utf-8') + self.data if self.data else b'')

        headers = []
        headers_done = False

        with BytesIO() as buf:
            print(f"Listening response from {self.method} {self.url} {self.VERSION}")
            while not headers_done:
                try:
                    resp = self.socket.recv(128)
                except BlockingIOError:
                    time.sleep(0.1)
                else:
                    buf.write(resp)
                    buf.seek(0)
                    start_index = 0
                    for line in buf:
                        start_index += len(line)
                        line = line.decode('ascii').rstrip()
                        if not line:
                            headers_done = True
                            break
                        headers.append(line)

                    if start_index:
                        buf.seek(start_index)
                        remaining = buf.read()
                        buf.truncate(0)
                        buf.seek(0)
                        buf.write(remaining)
                    else:
                        buf.seek(0, 2)
            out = RTSPResponse(headers)
            body_len = int(out.headers['content-length'])
            to_read = body_len - buf.getbuffer().nbytes
            while to_read > 0:
                try:
                    resp = self.socket.recv(to_read)
                except BlockingIOError:
                    time.sleep(0.1)
                else:
                    buf.write(resp)
                    to_read -= len(resp)
            buf.seek(0)
            out.body = buf.read(body_len)

        print(f"Completed response from {self.method} {self.url} {self.VERSION}")

        return out
