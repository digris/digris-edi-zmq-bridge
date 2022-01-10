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
parser.add_argument('--list-inputs', action="store_true", help='List inputs and their settings', required=False)

parser.add_argument('--enable-input', type=str, help='Enable input specified by hostname:port', required=False)
parser.add_argument('--disable-input', type=str, help='Disable input specified by hostname:port', required=False)
parser.add_argument('--switch-input', type=str, help='Enable input specified by hostname:port and disable all other inputs', required=False)

cli_args = parser.parse_args()

if cli_args.drop and cli_args.no_drop:
    print("You want to drop or not? Make up your mind!", file=sys.stderr)
    sys.exit(1)

s = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)
MY_SOCK_PATH = "/tmp/edi2edi-remote"
if os.path.exists(MY_SOCK_PATH):
    os.remove(MY_SOCK_PATH)
s.bind(MY_SOCK_PATH)

socket_path = cli_args.socket

def send_command(command):
    s.sendto(command.encode(), socket_path)
    dat, addr = s.recvfrom(4096)
    msg = dat.decode()
    try:
        # Round-trip through the json library so that it looks nice on console output
        # and can be reprocessed
        data = json.loads(msg)
        print(json.dumps(data, indent=2))
    except:
        print("Response is not JSON", file=sys.stderr)
        print("{}\n".format(msg))

if cli_args.get:
    print("Current settings:", file=sys.stderr)
    send_command("get settings")

if cli_args.drop:
    print("Enable late packet drop", file=sys.stderr)
    send_command("set drop-late 1")

if cli_args.no_drop:
    print("Disable late packet drop", file=sys.stderr)
    send_command("set drop-late 0")

if cli_args.delay:
    print(f"Setting delay to {cli_args.delay}", file=sys.stderr)
    send_command(f"set delay {cli_args.delay}")

if cli_args.drop_delay:
    print(f"Setting drop-delay to {cli_args.drop_delay}", file=sys.stderr)
    send_command(f"set drop-delay {cli_args.drop_delay}")

if cli_args.backoff:
    print(f"Setting backoff to {cli_args.backoff}", file=sys.stderr)
    send_command(f"set backoff {cli_args.backoff}")

if cli_args.list_inputs:
    print("Inputs:", file=sys.stderr)
    send_command("list inputs")

if cli_args.enable_input:
    print(f"Enabling input {cli_args.enable_input}", file=sys.stderr)
    send_command(f"set input enable {cli_args.enable_input}")

if cli_args.disable_input:
    print(f"Disable input {cli_args.disable_input}", file=sys.stderr)
    send_command(f"set input disable {cli_args.disable_input}")

if cli_args.switch_input:
    print(f"Switch input {cli_args.switch_input}", file=sys.stderr)
    send_command(f"switch input {cli_args.switch_input}")
