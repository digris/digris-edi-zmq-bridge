#!/usr/bin/env python3
import argparse
import json
import os
import socket
import sys

parser = argparse.ArgumentParser(description="Remote Control for ODR-EDI2EDI")
parser.add_argument('-s', '--socket', type=str, help='UNIX DGRAM socket path to send to', required=True)
parser.add_argument('--get', action="store_true", help='Get and display current settings', required=False)
parser.add_argument('-w', '--delay', type=int, help='Set the delay to the given value in milliseconds', required=False)
parser.add_argument('-b', '--backoff', type=int, help='Set the backoff to the given value in milliseconds', required=False)
parser.add_argument('-v', '--verbose', type=int, help='Set verbosity', required=False)
parser.add_argument('--stats', action="store_true", help='List inputs settings, stats and output stats', required=False)
parser.add_argument('--live-stats-port', type=int, help='Set live stats UDP port. Use 0 to disable.', required=False)
parser.add_argument('--reset-counters', action="store_true", help='Reset all statistics counters', required=False)

parser.add_argument('--enable-input', type=str, help='Enable input specified by hostname:port', required=False)
parser.add_argument('--disable-input', type=str, help='Disable input specified by hostname:port', required=False)

cli_args = parser.parse_args()

s = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)
s.settimeout(2)
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

if cli_args.delay is not None:
    print("Setting delay to {}".format(cli_args.delay), file=sys.stderr)
    send_command("set delay {}".format(cli_args.delay))

if cli_args.backoff is not None:
    print("Setting backoff to {}".format(cli_args.backoff), file=sys.stderr)
    send_command("set backoff {}".format(cli_args.backoff))

if cli_args.verbose is not None:
    print("Setting verbose to {}".format(cli_args.verbose), file=sys.stderr)
    send_command("set verbose {}".format(cli_args.verbose))

if cli_args.stats:
    print("Stats:", file=sys.stderr)
    send_command("stats")

if cli_args.enable_input:
    print("Enabling input {}".format(cli_args.enable_input), file=sys.stderr)
    send_command("set input enable {}".format(cli_args.enable_input))

if cli_args.disable_input:
    print("Disable input {}".format(cli_args.disable_input), file=sys.stderr)
    send_command("set input disable {}".format(cli_args.disable_input))

if cli_args.live_stats_port is not None:
    print("Set live stats port {}".format(cli_args.live_stats_port), file=sys.stderr)
    send_command("set live_stats_port {}".format(cli_args.live_stats_port))

if cli_args.reset_counters:
    print("Resetting counters", file=sys.stderr)
    send_command("reset counters")
