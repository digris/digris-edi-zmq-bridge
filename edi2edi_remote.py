#!/usr/bin/env python3
import argparse
import json
import os
import pprint
import socket
import sys

parser = argparse.ArgumentParser(description="Remote Control for ODR-EDI2EDI")
parser.add_argument('-s', '--socket', type=str, help='UNIX DGRAM socket path to send to', required=True)
parser.add_argument('--get', action="store_true", help='Get and display current settings', required=False)
parser.add_argument('-w', '--delay', type=int, help='Set the delay to the given value in milliseconds', required=False)
parser.add_argument('--drop', action="store_true", help='Enable dropping of late packets', required=False)
parser.add_argument('--no-drop', action="store_true", help='Disable dropping of late packets', required=False)
parser.add_argument('-x', '--drop-delay', type=int, help='Set the drop-delay to the given value in milliseconds', required=False)
parser.add_argument('-b', '--backoff', type=int, help='Set the backoff to the given value in milliseconds', required=False)

def send_command(socket_path, command):
    s.sendto(command.encode(), socket_path)
    dat, addr = s.recvfrom(4096)
    msg = dat.decode()
    try:
        pprint.pprint(json.loads(msg))
    except:
        print("Response is not JSON", file=sys.stderr)
        print("{}\n".format(msg))

cli_args = parser.parse_args()

if cli_args.drop and cli_args.no_drop:
    print("You want to drop or not? Make up your mind!", file=sys.stderr)
    sys.exit(1)

s = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)
MY_SOCK_PATH = "/tmp/edi2edi-remote"
if os.path.exists(MY_SOCK_PATH):
    os.remove(MY_SOCK_PATH)
s.bind(MY_SOCK_PATH)

if cli_args.get:
    print("Current settings:")
    send_command(cli_args.socket, "get settings")

if cli_args.drop:
    print("Enable late packet drop")
    send_command(cli_args.socket, "set drop-late 1")

if cli_args.no_drop:
    print("Disable late packet drop")
    send_command(cli_args.socket, "set drop-late 0")

if cli_args.delay:
    print(f"Setting delay to {cli_args.delay}")
    send_command(cli_args.socket, f"set delay {cli_args.delay}")

if cli_args.drop_delay:
    print(f"Setting drop-delay to {cli_args.drop_delay}")
    send_command(cli_args.socket, f"set drop-delay {cli_args.drop_delay}")

if cli_args.backoff:
    print(f"Setting backoff to {cli_args.backoff}")
    send_command(cli_args.socket, f"set backoff {cli_args.backoff}")
