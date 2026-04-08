#!/usr/bin/env python3
#
# The MIT License (MIT)
#
# Copyright (c) 2026 Matthias P. Braendli
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.

"""
Stats bridge to collect UDP stats from several instances

- Listens for UDP JSON packets on 0.0.0.0:4500
- Caches latest JSON object per source for 5 seconds
- Serves current cache as JSON on http://127.0.0.1:8005/stats.json
"""

import json
import select
import socket
import time
from typing import Dict, Tuple, Any

UDP_BIND = ("0.0.0.0", 4500)
HTTP_BIND = ("127.0.0.1", 8005)
CACHE_TTL_SECONDS = 5
POLL_TIMEOUT_MS = 500
MAX_UDP_PACKET_SIZE = 65535
MAX_HTTP_REQUEST_SIZE = 8192


class HTTPConnection:
    def __init__(self, sock: socket.socket, addr: Tuple[str, int]) -> None:
        self.sock = sock
        self.addr = addr
        self.in_buffer = bytearray()
        self.out_buffer = b""
        self.response_prepared = False


def prune_cache(cache: Dict[str, Dict[str, Any]], now: float) -> None:
    expired = [
        source
        for source, entry in cache.items()
        if now - entry["timestamp"] > CACHE_TTL_SECONDS
    ]
    for source in expired:
        del cache[source]


def build_stats_payload(cache: Dict[str, Dict[str, Any]], now: float) -> bytes:
    prune_cache(cache, now)

    payload = {
        source: entry["data"]
        for source, entry in cache.items()
    }
    return json.dumps(payload, separators=(",", ":"), ensure_ascii=False).encode("utf-8")


def make_http_response(
    status_code: int,
    reason: str,
    body: bytes,
    content_type: str = "application/json; charset=utf-8",
) -> bytes:
    headers = [
        f"HTTP/1.1 {status_code} {reason}",
        f"Content-Type: {content_type}",
        f"Content-Length: {len(body)}",
        "Connection: close",
        "Cache-Control: no-store",
        "",
        "",
    ]
    return "\r\n".join(headers).encode("ascii") + body


def parse_http_request_head(data: bytes) -> Tuple[str, str, str]:
    """
    Returns: (method, path, version)
    Raises ValueError on malformed request.
    """
    try:
        head = data.decode("iso-8859-1")
    except UnicodeDecodeError as exc:
        raise ValueError("request is not decodable") from exc

    lines = head.split("\r\n")
    if not lines or not lines[0]:
        raise ValueError("empty request line")

    parts = lines[0].split()
    if len(parts) != 3:
        raise ValueError("malformed request line")

    method, path, version = parts
    if not version.startswith("HTTP/"):
        raise ValueError("invalid HTTP version")

    return method, path, version


def prepare_http_response(conn: HTTPConnection, cache: Dict[str, Dict[str, Any]]) -> None:
    if conn.response_prepared:
        return

    if len(conn.in_buffer) > MAX_HTTP_REQUEST_SIZE:
        conn.out_buffer = make_http_response(
            413,
            "Payload Too Large",
            b"Request too large\n",
            "text/plain; charset=utf-8",
        )
        conn.response_prepared = True
        return

    header_end = conn.in_buffer.find(b"\r\n\r\n")
    if header_end == -1:
        return

    request_head = bytes(conn.in_buffer[:header_end])
    try:
        method, path, _version = parse_http_request_head(request_head)
    except ValueError:
        conn.out_buffer = make_http_response(
            400,
            "Bad Request",
            b"Bad Request\n",
            "text/plain; charset=utf-8",
        )
        conn.response_prepared = True
        return

    if method != "GET":
        conn.out_buffer = make_http_response(
            405,
            "Method Not Allowed",
            b"Method Not Allowed\n",
            "text/plain; charset=utf-8",
        )
        conn.response_prepared = True
        return

    if path != "/stats.json":
        conn.out_buffer = make_http_response(
            404,
            "Not Found",
            b"Not Found\n",
            "text/plain; charset=utf-8",
        )
        conn.response_prepared = True
        return

    body = build_stats_payload(cache, time.monotonic())
    conn.out_buffer = make_http_response(200, "OK", body)
    conn.response_prepared = True


def main() -> None:
    cache: Dict[str, Dict[str, Any]] = {}
    http_conns: Dict[int, HTTPConnection] = {}

    udp_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    udp_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    udp_sock.bind(UDP_BIND)
    udp_sock.setblocking(False)

    http_listen_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    http_listen_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    http_listen_sock.bind(HTTP_BIND)
    http_listen_sock.listen()
    http_listen_sock.setblocking(False)

    poller = select.poll()
    poller.register(udp_sock, select.POLLIN)
    poller.register(http_listen_sock, select.POLLIN)

    try:
        while True:
            now = time.monotonic()
            prune_cache(cache, now)

            events = poller.poll(POLL_TIMEOUT_MS)
            for fd, event in events:
                if fd == udp_sock.fileno():
                    if event & select.POLLIN:
                        while True:
                            try:
                                packet, addr = udp_sock.recvfrom(MAX_UDP_PACKET_SIZE)
                            except BlockingIOError:
                                break
                            except OSError as exc:
                                print(f"UDP recv error: {exc}")
                                break

                            host, port = addr
                            source = f"{host}:{port}"

                            try:
                                obj = json.loads(packet.decode("utf-8"))
                            except (UnicodeDecodeError, json.JSONDecodeError) as exc:
                                print(f"Ignoring invalid packet from {source}: {exc}")
                                continue

                            if not isinstance(obj, dict):
                                print(f"Ignoring non-object JSON packet from {source}")
                                continue


                            if "main" in obj and "unique_id" in obj["main"]:
                                source_key = obj["main"]["unique_id"]
                            else:
                                source_key = source

                            cache[source_key] = {
                                "timestamp": time.monotonic(),
                                "data": obj,
                            }

                elif fd == http_listen_sock.fileno():
                    if event & select.POLLIN:
                        while True:
                            try:
                                conn_sock, conn_addr = http_listen_sock.accept()
                            except BlockingIOError:
                                break
                            except OSError as exc:
                                print(f"HTTP accept error: {exc}")
                                break

                            conn_sock.setblocking(False)
                            conn = HTTPConnection(conn_sock, conn_addr)
                            http_conns[conn_sock.fileno()] = conn
                            poller.register(conn_sock, select.POLLIN)

                else:
                    conn = http_conns.get(fd)
                    if conn is None:
                        continue

                    close_conn = False

                    if event & (select.POLLERR | select.POLLHUP | select.POLLNVAL):
                        close_conn = True

                    if not close_conn and (event & select.POLLIN):
                        while True:
                            try:
                                chunk = conn.sock.recv(4096)
                            except BlockingIOError:
                                break
                            except OSError:
                                close_conn = True
                                break

                            if not chunk:
                                close_conn = True
                                break

                            conn.in_buffer.extend(chunk)

                            if not conn.response_prepared:
                                prepare_http_response(conn, cache)
                                if conn.response_prepared:
                                    poller.modify(conn.sock, select.POLLOUT)
                                    break

                    if not close_conn and (event & select.POLLOUT):
                        try:
                            sent = conn.sock.send(conn.out_buffer)
                            conn.out_buffer = conn.out_buffer[sent:]
                        except BlockingIOError:
                            sent = 0
                        except OSError:
                            close_conn = True

                        if not close_conn and not conn.out_buffer:
                            close_conn = True

                    if close_conn:
                        try:
                            poller.unregister(conn.sock)
                        except OSError:
                            pass
                        try:
                            conn.sock.close()
                        except OSError:
                            pass
                        http_conns.pop(fd, None)

    except KeyboardInterrupt:
        print("Shutting down.")
    finally:
        for conn in list(http_conns.values()):
            try:
                poller.unregister(conn.sock)
            except OSError:
                pass
            try:
                conn.sock.close()
            except OSError:
                pass

        try:
            poller.unregister(udp_sock)
        except OSError:
            pass
        try:
            poller.unregister(http_listen_sock)
        except OSError:
            pass

        udp_sock.close()
        http_listen_sock.close()


if __name__ == "__main__":
    main()
