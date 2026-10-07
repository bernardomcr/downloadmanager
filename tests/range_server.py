#!/usr/bin/env python3
"""Servidor HTTP de teste para o motor de download.

Serve um arquivo com suporte a Range, limitando a velocidade por conexão (para mostrar o ganho
de várias conexões) e, opcionalmente, derrubando conexões no meio (para testar a recuperação).

    python3 tests/range_server.py ARQUIVO [--port 8765] [--rate 2000000] [--drop 0.0] [--no-range]
"""
import argparse
import hashlib
import os
import random
import socket
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer


def make_handler(path, rate, drop, no_range, stats):
    size = os.path.getsize(path)
    with open(path, "rb") as f:
        etag = '"%s"' % hashlib.md5(f.read(1 << 20)).hexdigest()

    class Handler(BaseHTTPRequestHandler):
        protocol_version = "HTTP/1.1"

        def log_message(self, *args):
            pass

        def do_GET(self):
            with stats["lock"]:
                stats["requests"] += 1
            if self.path.startswith("/redirect"):
                self.send_response(302)
                self.send_header("Location", "/files/" + os.path.basename(path))
                self.send_header("Content-Length", "0")
                self.end_headers()
                return
            if self.path.startswith("/expired"):
                self.send_response(403)
                self.send_header("Content-Length", "0")
                self.end_headers()
                return

            start, end = 0, size - 1
            header = self.headers.get("Range")
            partial = False
            if header and not no_range and header.startswith("bytes="):
                first, _, last = header[6:].partition("-")
                start = int(first)
                end = min(int(last), size - 1) if last else size - 1
                partial = True

            self.send_response(206 if partial else 200)
            self.send_header("Content-Type", "application/octet-stream")
            self.send_header("Content-Length", str(end - start + 1))
            self.send_header("ETag", etag)
            if partial:
                self.send_header("Content-Range", "bytes %d-%d/%d" % (start, end, size))
            self.send_header("Content-Disposition", "attachment; filename*=UTF-8''arquivo%20de%20teste%20%C3%A7.bin")
            self.end_headers()

            chunk = 16 * 1024
            with open(path, "rb") as f:
                f.seek(start)
                remaining = end - start + 1
                began = time.monotonic()
                sent = 0
                while remaining > 0:
                    data = f.read(min(chunk, remaining))
                    if drop and random.random() < drop:
                        # shutdown derruba de verdade (close sozinho não fecha enquanto rfile/wfile existem).
                        self.connection.shutdown(socket.SHUT_RDWR)
                        self.close_connection = True
                        return
                    try:
                        self.wfile.write(data)
                    except (BrokenPipeError, ConnectionResetError):
                        return
                    remaining -= len(data)
                    sent += len(data)
                    if rate:
                        ahead = sent / rate - (time.monotonic() - began)
                        if ahead > 0:
                            time.sleep(ahead)

    return Handler


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("file")
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--rate", type=int, default=0, help="bytes/s por conexão (0 = sem limite)")
    parser.add_argument("--drop", type=float, default=0.0, help="chance de derrubar a conexão a cada 16 KB")
    parser.add_argument("--no-range", action="store_true")
    args = parser.parse_args()

    stats = {"requests": 0, "lock": threading.Lock()}
    server = ThreadingHTTPServer(("127.0.0.1", args.port),
                                 make_handler(args.file, args.rate, args.drop, args.no_range, stats))
    server.daemon_threads = True
    server.serve_forever()


if __name__ == "__main__":
    main()
