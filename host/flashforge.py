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
        else:
            print(f"unknown command {cmd}")
            return 1

    print("board alive" if ok else "no response - is it in update mode?")
    return 0 if ok else 2


if __name__ == "__main__":
    sys.exit(main())