#!/usr/bin/env python3
"""Finge ser o navegador falando com o dm-host.exe (Native Messaging): útil para testar a ponte.

    python3 tests/native_host_client.py "wine build-mingw/dm-host.exe" '{"type":"ping"}' ...

Cada argumento depois do comando é uma mensagem JSON; imprime cada resposta.
Uma mensagem "@wait-started <token>" pergunta o andamento até o app responder algo diferente de "waiting".
"""
import json
import shlex
import struct
import subprocess
import sys
import time


def send(process, message):
    data = json.dumps(message).encode()
    process.stdin.write(struct.pack("<I", len(data)) + data)
    process.stdin.flush()
    length = struct.unpack("<I", process.stdout.read(4))[0]
    return json.loads(process.stdout.read(length))


def main():
    process = subprocess.Popen(shlex.split(sys.argv[1]), stdin=subprocess.PIPE, stdout=subprocess.PIPE)
    for argument in sys.argv[2:]:
        if argument.startswith("@wait-started "):
            token = argument.split(" ", 1)[1]
            for _ in range(60):
                reply = send(process, {"type": "status", "token": token})
                if reply.get("status") != "waiting":
                    break
                time.sleep(1)
            print("status:", reply, flush=True)
            continue
        print(send(process, json.loads(argument)), flush=True)
    process.stdin.close()
    process.wait(timeout=10)


if __name__ == "__main__":
    main()
