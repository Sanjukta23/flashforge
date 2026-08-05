# Building & Running FlashForge

**Hardware:** Nucleo-F446RE + one USB cable.
**Tools:** STM32CubeIDE, STM32CubeProgrammer, Python 3 + `pyserial`, PuTTY.

## Memory map

| Address     | Contents                          |
|-------------|-----------------------------------|
| 0x08000000  | Bootloader (sectors 0-1, 32 KB)   |
| 0x08008000  | App header (magic/version/len/crc)|
| 0x08008200  | App + vector table                |

## Build

1. Open `bootloader` and `app` in CubeIDE, build both (0 errors/warnings).
2. Each project needs binary output on:
   Properties -> C/C++ Build -> Settings -> MCU Post build outputs -> Convert to binary.
3. Make the flashable image:
   ```
   cd host
   py flashforge.py --makeimg ..\app\Debug\app.bin app_full.bin
   ```

## Flash (CubeProgrammer -- do NOT enable full chip erase)

- `bootloader.bin` -> 0x08000000
- `app_full.bin`   -> 0x08008000
- Disconnect, reset. Expect: 5 fast flashes, then a steady blink.

## Update mode

Bootloader listens on USART2 (115200 8N1) when the blue button is held at reset
**or** the app header is invalid.

## Host tool (run from `host/`, close PuTTY first)

```
py flashforge.py COM3 --ping           check board responds
py flashforge.py COM3 --ping-corrupt   corrupted frame -> NACK -> retry
```

## Demos

- **Ping/retry:** `--ping` prints `board alive`; `--ping-corrupt` shows a NACK then
  a successful retry.
- **Failsafe:** flash a valid image, corrupt one byte at ~0x08009000 in the memory
  viewer, reset -> board stays in update mode. Re-flash `app_full.bin` -> boots again.

## Common issues

- CubeIDE "Failed to start GDB server" -> CubeProgrammer/PuTTY is holding the ST-Link.
- Valid image rejected -> host and bootloader magic values differ, or CRC clock off.
- `python` not found on Windows -> use `py`.
