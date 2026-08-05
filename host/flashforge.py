"""FlashForge host tool - talks the protocol.md protocol to the bootloader."""

import struct
import sys
import time

import serial

SOF  = 0xA5
ACK  = 0x79
NACK = 0x1F

APP_MAGIC = 0x464C4652

CMD_PING  = 0x01
CMD_ERASE = 0x02
CMD_WRITE = 0x03
CMD_GO    = 0x05

REPLY_TIMEOUT_S = 2.0
MAX_RETRIES     = 3

APP_BODY_ADDR = 0x08008200      # app + vector table
HDR_ADDR      = 0x08008000      # header block
CHUNK         = 128             # data bytes per WRITE frame (<= 251)
ERASE_SECTORS = [2, 3]          # covers 0x08008000 - 0x0800FFFF

def crc32_mpeg2(data: bytes) -> int:
    """CRC-32/MPEG-2, matches the F446 hardware CRC. Pads tail with 0x00."""
    if len(data) % 4:
        data = data + b"\x00" * (4 - len(data) % 4)

    crc = 0xFFFFFFFF
    for byte in data:
        crc ^= byte << 24
        for _ in range(8):
            if crc & 0x80000000:
                crc = ((crc << 1) ^ 0x04C11DB7) & 0xFFFFFFFF
            else:
                crc = (crc << 1) & 0xFFFFFFFF
    return crc

def make_image(app_path: str, out_path: str, version: int = 1) -> None:
    """Read app.bin, prepend a 512-byte header block, write app_full.bin.

    Header (16 bytes, little-endian) then 0xFF padding to 0x200:
      [MAGIC][VERSION][LENGTH][CRC32]
    App bytes then follow at offset 0x200 -> flashes to 0x08008200.
    """
    with open(app_path, "rb") as f:
        app = f.read()

    crc = crc32_mpeg2(app)
    header = struct.pack("<IIII", APP_MAGIC, version, len(app), crc)
    header_block = header + b"\xff" * (0x200 - len(header))   # pad gap with 'no ink'

    with open(out_path, "wb") as f:
        f.write(header_block + app)

    print(f"image: {len(app)} app bytes, crc={crc:08X}, "
          f"total={0x200 + len(app)} bytes -> {out_path}")

    
def build_frame(cmd: int, payload: bytes = b"") -> bytes:
    """[SOF][LEN][CMD][PAYLOAD][CRC32-LE] - CRC covers LEN+CMD+PAYLOAD."""
    assert len(payload) <= 255
    body = bytes([len(payload), cmd]) + payload
    crc  = crc32_mpeg2(body)
    return bytes([SOF]) + body + struct.pack("<I", crc)


def send_frame(port: serial.Serial, cmd: int, payload: bytes = b"",
               corrupt: bool = False) -> bool:
    """Send one frame, wait for ACK. Retries on NACK/timeout."""
    frame = build_frame(cmd, payload)

    for attempt in range(1, MAX_RETRIES + 1):
        tx = frame
        if corrupt and attempt == 1:
            broken = bytearray(tx)
            broken[2] ^= 0x01
            tx = bytes(broken)
            print("  [test] sending deliberately corrupted frame")

        port.reset_input_buffer()
        port.write(tx)

        reply = port.read(1)
        if reply and reply[0] == ACK:
            return True
        reason = "NACK" if (reply and reply[0] == NACK) else "timeout"
        print(f"  attempt {attempt}/{MAX_RETRIES}: {reason} - retrying")

    return False

def erase_sector(port, sector: int) -> bool:
    return send_frame(port, CMD_ERASE, bytes([sector]))


def write_chunk(port, addr: int, data: bytes) -> bool:
    """One WRITE frame: 4-byte LE address + word-aligned data."""
    if len(data) % 4:                       # pad tail to a word with 0xFF (blank flash)
        data = data + b"\xff" * (4 - len(data) % 4)
    payload = struct.pack("<I", addr) + data
    return send_frame(port, CMD_WRITE, payload)


def write_region(port, base_addr: int, blob: bytes, label: str) -> bool:
    """Stream a blob to flash in CHUNK-sized WRITE frames."""
    for off in range(0, len(blob), CHUNK):
        chunk = blob[off:off + CHUNK]
        if not write_chunk(port, base_addr + off, chunk):
            print(f"  {label}: write failed at offset {off}")
            return False
        done = min(off + CHUNK, len(blob))
        print(f"  {label}: {done}/{len(blob)} bytes", end="\r")
    print()
    return True


def write_image(port, image_path: str) -> bool:
    """Flash app_full.bin (header + pad + app) over UART.

    Order is the failsafe: erase, write the APP BODY first, write the
    HEADER LAST. An interrupted update leaves no valid header, so the
    bootloader rejects the partial image and stays in update mode.
    """
    with open(image_path, "rb") as f:
        image = f.read()

    header_block = image[:0x200]     # the 512-byte header+pad block
    app_body     = image[0x200:]     # everything after -> flashes to 0x08008200

    print("ping...")
    if not send_frame(port, CMD_PING):
        print("no response - is the board in update mode?")
        return False

    print(f"erasing sectors {ERASE_SECTORS}...")
    for s in ERASE_SECTORS:
        if not erase_sector(port, s):
            print(f"  erase of sector {s} failed")
            return False

    print("writing app body...")
    if not write_region(port, APP_BODY_ADDR, app_body, "app"):
        return False

    print("writing header (last)...")
    if not write_region(port, HDR_ADDR, header_block, "hdr"):
        return False

    print("go...")
    if not send_frame(port, CMD_GO):
        print("  GO not acked")
        return False

    print("done - board should boot the new firmware")
    return True

def main() -> int:
    if len(sys.argv) < 2:
        print("usage:")
        print("  python flashforge.py --makeimg app.bin app_full.bin")
        print("  python flashforge.py COMx --ping | --ping-corrupt")
        return 1

    # port-less commands first
    if sys.argv[1] == "--makeimg":
        if len(sys.argv) != 4:
            print("usage: python flashforge.py --makeimg <in.bin> <out.bin>")
            return 1
        make_image(sys.argv[2], sys.argv[3])
        return 0

    # everything below needs the serial port
    com = sys.argv[1]
    cmd = sys.argv[2]

    with serial.Serial(com, 115200, timeout=REPLY_TIMEOUT_S) as port:
        time.sleep(0.1)
        if cmd == "--ping":
            ok = send_frame(port, CMD_PING)
        elif cmd == "--ping-corrupt":
            ok = send_frame(port, CMD_PING, corrupt=True)
        elif cmd == "--flash":
            if len(sys.argv) != 4:
                print("usage: python flashforge.py COMx --flash <image.bin>")
                return 1
            ok = write_image(port, sys.argv[3])
        else:
            print(f"unknown command {cmd}")
            return 1

    print("board alive" if ok else "no response - is it in update mode?")
    return 0 if ok else 2


if __name__ == "__main__":
    sys.exit(main())