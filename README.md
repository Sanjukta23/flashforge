# Flashforge

## Flash Memory Map (STM32F446RE — 512 KB)

| Sector | Start address | Size   | Owner              |
|--------|---------------|--------|--------------------|
| 0      | 0x0800 0000   | 16 KB  | Bootloader         |
| 1      | 0x0800 4000   | 16 KB  | Bootloader         |
| 2      | 0x0800 8000   | 16 KB  | App header + app   |
| 3      | 0x0800 C000   | 16 KB  | Application        |
| 4      | 0x0801 0000   | 64 KB  | Application        |
| 5      | 0x0802 0000   | 128 KB | Application        |
| 6      | 0x0804 0000   | 128 KB | Application        |
| 7      | 0x0806 0000   | 128 KB | Application        |

**Plan:** Sectors 0–1 (32 KB) hold the bootloader at 0x08000000.
The application region begins at sector 2: the app header (magic | version |
length | CRC32) sits at 0x08008000, with the application image and its
vector table at 0x08008100.