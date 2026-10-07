#!/usr/bin/env python3
"""
File2TCP transport test receiver.

Original prototype: 2024-06-05
Public hardening pass: 2026-10-08

This remains a transport test tool. It is not the proposed himawari-rx TCP
input implementation; see README.md for that integration design.
"""

import argparse
import socket
from pathlib import Path


def parse_args():
    parser = argparse.ArgumentParser(description="Minimal binary-safe TCP receiver for File2TCP testing")
    parser.add_argument("--host", default="0.0.0.0", help="listen address (default: 0.0.0.0)")
    parser.add_argument("--port", type=int, default=9999, help="listen port (default: 9999)")
    parser.add_argument("--output", type=Path, help="optional file to append received bytes to")
    return parser.parse_args()


def main():
    args = parse_args()
    if not 1 <= args.port <= 65535:
        raise SystemExit("port must be between 1 and 65535")

    output = args.output.open("ab") if args.output else None

    try:
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as server_socket:
            server_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            server_socket.bind((args.host, args.port))
            server_socket.listen(socket.SOMAXCONN)
            print(f"Listening on {args.host}:{args.port}")

            while True:
                client_socket, client_address = server_socket.accept()
                print(f"Connected: {client_address[0]}:{client_address[1]}")
                session_bytes = 0

                with client_socket:
                    while True:
                        data = client_socket.recv(65536)
                        if not data:
                            break

                        session_bytes += len(data)
                        if output:
                            output.write(data)
                            output.flush()

                        print(f"Received {session_bytes} bytes in this session", end="\r", flush=True)

                print(f"\nDisconnected: {client_address[0]}:{client_address[1]} ({session_bytes} bytes)")
    except KeyboardInterrupt:
        print("\nStopped")
    finally:
        if output:
            output.close()


if __name__ == "__main__":
    main()
