
# Engineering notes

The "why" behind each milestone. One line per decision.

## M1 — Memory map
- Bootloader = sectors 0-1 (no single 32 KB sector on the F446).
- Setting FLASH LENGTH = 32K makes the build fail if the bootloader overruns — linker as safety fence.
- Verified section placement in the `.map` files, not by assuming.

## M2 — App at offset + VTOR
- Chip always boots from 0x08000000; to run elsewhere, link at the offset AND set `SCB->VTOR`, or the first interrupt hardfaults.
- Vector table needs 512-byte alignment (VTOR low bits are hardwired 0) — this drives the 0x200 header gap in M7.

## M3 — Jump
- Boot = two words: [0] MSP, [4] reset handler. Bootloader repeats this manually.
- Read both words before setting MSP.
- Must stop SysTick before jumping, else an interrupt fires mid-handover and hardfaults (#1 first-bootloader bug).

## M4 — Update mode
- Blue button = PC13, active-low (0 = pressed).
- USART2 @ 115200 8N1.
- Button is the manual trigger; bad CRC is the automatic one (M7).

## M5 — Flash driver
- Writing only clears bits (1->0); erasing resets a whole sector to 0xFF. Hence erase-before-write.
- Erase takes a sector number, program takes an address — asymmetry the protocol mirrors.
- Controller locked at reset; two keys unlock it; error flags are write-1-to-clear, cleared before each op.
- Erase visibly freezes the CPU (runs from the same flash).

## M6 — Protocol (hardest)
- Frame: `[0xA5][LEN][CMD][PAYLOAD][CRC32]`. CRC covers LEN+CMD+PAYLOAD; all fields little-endian.
- State machine WAIT_SOF->LEN->CMD->PAYLOAD->CRC; timeout is state-dependent (block between frames, 100 ms mid-frame).
- 0xA5 in a payload is harmless — past WAIT_SOF the receiver counts, doesn't scan.
- Single-byte ACK/NACK, no sequence numbers — safe because all commands are idempotent.
- F446 CRC is CRC-32/MPEG-2 (no reflection, no final XOR); `zlib.crc32` does NOT match.
- Byte-packing (first byte -> top of word) specified in the spec first, so board and host agreed on `0x81DA1A18` first try.

## M7 — Header validation (failsafe)
- Header at 0x08008000: `[MAGIC][VERSION][LENGTH][CRC32]`, written by the host tool (an app can't hold its own CRC).
- App vector table at 0x08008200 (512-aligned); gap padded with 0xFF (matches erased flash).
- Boot check order: magic -> length sanity -> CRC. Length check stops a garbage length from running the CRC loop off into a busfault.
- Same `crc_compute` used for frames (M6) and images (M7).
- Failsafe = ordering: host writes the app first, header last, so an interrupted update leaves no valid header. Bootloader sectors are never touched, so the board can't brick.

## M8 — OTA update over UART
- `flashforge.py --flash` replaces CubeProgrammer: PING -> ERASE sectors 2-3 -> WRITE app body -> WRITE header last -> GO.
- Header-last ordering is the failsafe: an interrupted flash leaves no valid header, so the board rejects the partial image.
- WRITE chunks are 128 data bytes + 4-byte address; final chunk padded to a word with 0xFF.
- v2 demo app reads the internal temp sensor (ADC1 channel 18) and prints degC every second.
  - Temp sensor needs a long ADC sample time (480 cycles) — 3 cycles gives jittery/garbage reads.

### M8 debugging story (worth telling)
- **Symptom:** after an OTA flash, v2 printed one temperature line then froze. Under the debugger the same binary streamed continuously.
- **Diagnosis:** the difference is the entry path. The debugger enters the app via a hardware reset; the OTA path enters via `jump_to_app()`. Proved the app logic was fine, so the fault was in the handover.
- **Cause:** `jump_to_app()` did `__disable_irq()` and never re-enabled interrupts, so the app's `HAL_Delay` hung waiting for a SysTick tick that could not fire.
- **Fix:** in `jump_to_app()`, before the jump — clear all pending NVIC interrupts and call `__enable_irq()`, so the app inherits a reset-equivalent interrupt state. Handover now behaves exactly like a hardware reset.