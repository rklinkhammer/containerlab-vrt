#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import socket
import struct
from typing import cast

MAX_REQUEST = 1024
MAX_RESPONSE = 4096
TIMEOUT_SECONDS = 2.0


def receive_exact(connection: socket.socket, size: int) -> bytes:
    result = bytearray()
    while len(result) < size:
        block = connection.recv(size - len(result))
        if not block:
            raise RuntimeError("status connection closed before the complete frame")
        result.extend(block)
    return bytes(result)


def query(host: str, port: int) -> dict[str, object]:
    payload = json.dumps(
        {"version": 1, "operation": "get_status"}, separators=(",", ":")
    ).encode("utf-8")
    if len(payload) > MAX_REQUEST:
        raise RuntimeError("internal request exceeds protocol limit")
    with socket.create_connection((host, port), timeout=TIMEOUT_SECONDS) as connection:
        connection.settimeout(TIMEOUT_SECONDS)
        connection.sendall(struct.pack("!I", len(payload)) + payload)
        response_size = struct.unpack("!I", receive_exact(connection, 4))[0]
        if response_size > MAX_RESPONSE:
            raise RuntimeError("status response exceeds 4096-byte protocol limit")
        response = json.loads(receive_exact(connection, response_size))
        if not isinstance(response, dict):
            raise RuntimeError("status response is not a JSON object")
        return cast(dict[str, object], response)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", required=True)
    parser.add_argument("--port", required=True, type=int)
    arguments = parser.parse_args()
    if not 1 <= arguments.port <= 65535:
        raise SystemExit("port must be in 1..65535")
    print(json.dumps(query(arguments.host, arguments.port), sort_keys=True))


if __name__ == "__main__":
    main()
