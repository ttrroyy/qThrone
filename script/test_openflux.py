"""Exercise the bundled client against a local L4 exit, without public accounts."""
import http.client
import http.server
import json
import os
from pathlib import Path
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import time


def unused_port():
    with socket.socket() as sock:
        sock.bind(('127.0.0.1', 0))
        return sock.getsockname()[1]


def exact(sock, n):
    data = b''
    while len(data) < n:
        chunk = sock.recv(n - len(data))
        if not chunk:
            raise RuntimeError('SOCKS connection closed')
        data += chunk
    return data


USER, PASSWORD = b'qthrone-test-user', b'qthrone-test-pass'


def socks(port, command, host, target_port):
    sock = socket.create_connection(('127.0.0.1', port), timeout=10)
    sock.sendall(b'\x05\x01\x02')
    assert exact(sock, 2) == b'\x05\x02', 'SOCKS greeting failed'
    sock.sendall(b'\x01' + bytes([len(USER)]) + USER + bytes([len(PASSWORD)]) + PASSWORD)
    assert exact(sock, 2) == b'\x01\x00', 'SOCKS authentication failed'
    sock.sendall(b'\x05' + bytes([command, 0, 1]) + socket.inet_aton(host) + struct.pack('!H', target_port))
    reply = exact(sock, 4)
    assert reply[:2] == b'\x05\x00', 'SOCKS request failed'
    assert reply[3] == 1, 'expected IPv4 reply'
    address = socket.inet_ntoa(exact(sock, 4))
    bound = struct.unpack('!H', exact(sock, 2))[0]
    return sock, (address, bound)


class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def do_GET(self):
        data = b'OpenFlux-test' * 81920
        self.send_response(200)
        self.send_header('Content-Length', str(len(data)))
        self.end_headers()
        self.wfile.write(data)


def main(binary, exit_binary):
    binary = str(Path(binary).resolve())
    exit_binary = str(Path(exit_binary).resolve())
    # Use the host's IPv4 interface: a userspace stack owns its own loopback.
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as route:
        route.connect(('192.0.2.1', 9))
        host = route.getsockname()[0]
    server = http.server.ThreadingHTTPServer(('0.0.0.0', 0), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    udp.bind(('0.0.0.0', 0))
    udp.settimeout(15)

    def echo():
        try:
            data, sender = udp.recvfrom(2048)
            udp.sendto(data, sender)
        except OSError:
            pass

    threading.Thread(target=echo, daemon=True).start()
    processes = []
    try:
        with tempfile.TemporaryDirectory(prefix='qthrone-openflux-test-') as folder:
            folder = Path(folder)
            secret = 'local-integration-test-secret'
            key = folder / 'exit.key'
            key.write_text(secret)
            os.chmod(key, 0o600)
            carrier_port, socks_port = unused_port(), unused_port()
            config = folder / 'exit.conf'
            config.write_text(f'[Interface]\nRole = exit\nMode = l4\nCodec = batched\nEncryptionKeyFile = {key}\nSessionContext = http://openflux\nURL = http://openflux\n\n[Transport "direct"]\nType = direct\nPriority = 100\nListen = 127.0.0.1:{carrier_port}\n')
            private = folder / 'client.json'
            private.write_text(json.dumps({'negotiate': True, 'secret': secret, 'context': 'http://openflux',
                'socks': f'127.0.0.1:{socks_port}', 'socks_user': USER.decode(), 'socks_pass': PASSWORD.decode(), 'transports': [{'type': 'direct', 'priority': 100, 'dial': f'127.0.0.1:{carrier_port}'}]}))
            os.chmod(private, 0o600)
            flags = subprocess.CREATE_NO_WINDOW if os.name == 'nt' else 0
            for command in ([exit_binary, '--config', str(config), '--negotiate'], [binary, '--qthrone-config', str(private)]):
                processes.append(subprocess.Popen(command, stdin=subprocess.PIPE,
                    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, creationflags=flags))
                time.sleep(0.5)
            deadline = time.monotonic() + 30
            while True:
                assert all(p.poll() is None for p in processes), 'OpenFlux exited during startup'
                try:
                    # The helper opens its listener only once the tunnel is up.
                    with socket.create_connection(('127.0.0.1', socks_port), timeout=0.2):
                        break
                except OSError:
                    assert time.monotonic() < deadline, 'OpenFlux startup timed out'
                    time.sleep(0.1)
            for _ in range(3):
                sock, _ = socks(socks_port, 1, host, server.server_port)
                with sock:
                    sock.sendall(b'GET / HTTP/1.0\r\nHost: local-test\r\n\r\n')
                    response = http.client.HTTPResponse(sock)
                    response.begin()
                    assert response.status == 200 and response.read() == b'OpenFlux-test' * 81920, 'tunnel HTTP transfer failed'
            control, relay = socks(socks_port, 3, '0.0.0.0', 0)
            with control, socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as client:
                client.settimeout(10)
                relay_host = relay[0] if relay[0] != '0.0.0.0' else '127.0.0.1'
                packet = b'\x00\x00\x00\x01' + socket.inet_aton(host) + struct.pack('!H', udp.getsockname()[1]) + b'OpenFlux-UDP'
                client.sendto(packet, (relay_host, relay[1]))
                result, _ = client.recvfrom(2048)
                assert result.endswith(b'OpenFlux-UDP'), 'tunnel UDP transfer failed'
            processes[1].stdin.write(b'STOP\n')
            processes[1].stdin.flush()
            processes[1].wait(timeout=3)
            print('OpenFlux: repeated HTTP, bulk transfer, UDP and shutdown passed')
    finally:
        for process in reversed(processes):
            if process.poll() is None:
                process.kill()
            process.wait(timeout=5)
        server.shutdown()
        udp.close()


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
