#!/usr/bin/env python3
"""One-time, approved D2 scratch-page acceptance on the physical W25Q32.

The firmware command only programs a fixed 16-byte pattern at 0x3FF000 after
blank-checking it. This script never retries the program command and never
sends an erase command. Do not use for an unapproved or non-disposable module.
"""

import argparse
import sys
import time
from pathlib import Path

import serial

from spi_flash_readonly import response


EXPECTED = "EF4016"


def open_console(port):
    device = serial.Serial(port, 115200, timeout=0.25)
    device.dtr = False
    device.rts = False
    time.sleep(2)
    return device


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--log", type=Path, required=True)
    parser.add_argument("--approve-once", action="store_true",
                        help="confirm that 0x3FF000 is reserved as disposable D2 scratch")
    args = parser.parse_args()
    if not args.approve_once:
        parser.error("--approve-once is required before any program operation")
    args.log.parent.mkdir(parents=True, exist_ok=True)
    try:
        with args.log.open("w", encoding="utf-8") as log:
            log.write("# External W25Q32: one approved 16-byte program; no erase; no automatic program retry\n")
            with open_console(args.port) as device:
                scan = response(device, log, f"SPISCAN {EXPECTED}", "[SPISCAN]")
                if "[SPISCAN] BLANK sector=3FF000 bytes=4096" not in scan:
                    raise RuntimeError("candidate scratch sector is not blank; program was not sent")
                program = response(device, log, f"SPIPROGRAM {EXPECTED}", "[SPIPROGRAM]")
                if "[SPIPROGRAM] PASS address=3FF000 bytes=16 readback=verified erase=none" not in program:
                    raise RuntimeError("program did not verify; command will not be retried")
                verify = response(device, log, f"SPIVERIFY {EXPECTED}", "[SPIVERIFY]")
                if "[SPIVERIFY] PASS address=3FF000 bytes=16 pattern=retained" not in verify:
                    raise RuntimeError("immediate readback check failed")
                log.write("# resetting ESP32-S3 via USB RTS; external flash power remains connected\n")
                log.flush()
                device.rts = True
                time.sleep(0.1)
                device.rts = False
            with open_console(args.port) as device:
                after = response(device, log, f"SPIVERIFY {EXPECTED}", "[SPIVERIFY]")
                if "[SPIVERIFY] PASS address=3FF000 bytes=16 pattern=retained" not in after:
                    raise RuntimeError("post-reset verification failed")
    except (OSError, serial.SerialException, TimeoutError, RuntimeError) as exc:
        print(f"FAIL: {exc}; do not repeat the program command; log: {args.log}", file=sys.stderr)
        return 1
    for line in (scan, program, verify, after):
        print(line)
    print(f"PASS: one-time program, readback and reset retention; log: {args.log}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
