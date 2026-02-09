#!/usr/bin/env python3
"""
Build LittleFS filesystem image for Tufty 2040 Badge

This script:
1. Creates a LittleFS filesystem image from a directory of PNG files
2. Creates a UF2 file for the filesystem
3. Optionally combines firmware + filesystem into one UF2

Usage:
    ./build_filesystem.py <image_dir> [--firmware tufty_badge.uf2]

Example:
    ./build_filesystem.py ../pics --firmware build/tufty_badge.uf2
"""

import os
import sys
import struct
import argparse
from pathlib import Path

try:
    from littlefs import LittleFS
except ImportError:
    print("Error: littlefs-python not installed")
    print("Run: pip install littlefs-python")
    sys.exit(1)

# Tufty 2040 flash configuration
FLASH_SIZE = 8 * 1024 * 1024      # 8MB total flash
FS_SIZE = 2 * 1024 * 1024         # 2MB filesystem
FS_OFFSET = FLASH_SIZE - FS_SIZE  # Filesystem starts here (6MB)
BLOCK_SIZE = 4096                 # Flash sector size (FLASH_SECTOR_SIZE)
BLOCK_COUNT = FS_SIZE // BLOCK_SIZE
PROG_SIZE = 256                   # Flash page size (FLASH_PAGE_SIZE)
READ_SIZE = 1
CACHE_SIZE = BLOCK_SIZE // 4      # 1024 bytes
LOOKAHEAD_SIZE = 32

# RP2040 flash base address
FLASH_BASE = 0x10000000

# UF2 constants
UF2_MAGIC_START0 = 0x0A324655
UF2_MAGIC_START1 = 0x9E5D5157
UF2_MAGIC_END = 0x0AB16F30
UF2_FLAG_FAMILY = 0x00002000
RP2040_FAMILY_ID = 0xe48bff56


def create_uf2_block(data, block_no, num_blocks, target_addr, family_id):
    """Create a single UF2 block"""
    assert len(data) <= 256
    data = data.ljust(476, b'\x00')  # Pad to 476 bytes

    block = struct.pack('<IIIIIIII',
        UF2_MAGIC_START0,
        UF2_MAGIC_START1,
        UF2_FLAG_FAMILY,
        target_addr,
        256,
        block_no,
        num_blocks,
        family_id
    )
    block += data
    block += struct.pack('<I', UF2_MAGIC_END)

    assert len(block) == 512
    return block


def binary_to_uf2(data, start_addr, family_id=RP2040_FAMILY_ID):
    """Convert binary data to UF2 format"""
    num_blocks = (len(data) + 255) // 256
    uf2_data = b''

    for i in range(num_blocks):
        chunk = data[i*256:(i+1)*256]
        addr = start_addr + i * 256
        uf2_data += create_uf2_block(chunk, i, num_blocks, addr, family_id)

    return uf2_data


def combine_uf2_files(uf2_files):
    """Combine multiple UF2 files, renumbering blocks"""
    all_blocks = []

    for uf2_data in uf2_files:
        for i in range(0, len(uf2_data), 512):
            block = bytearray(uf2_data[i:i+512])
            # Extract and store block data (address and payload)
            all_blocks.append(block)

    # Renumber all blocks
    total_blocks = len(all_blocks)
    result = b''

    for i, block in enumerate(all_blocks):
        # Update block_no and num_blocks in the header
        struct.pack_into('<I', block, 20, i)           # block_no
        struct.pack_into('<I', block, 24, total_blocks) # num_blocks
        result += bytes(block)

    return result


def build_filesystem(image_dir, output_file):
    """Build LittleFS filesystem image from directory"""
    image_dir = Path(image_dir)

    if not image_dir.exists():
        print(f"Error: Directory {image_dir} does not exist")
        return None

    # Find PNG files
    png_files = list(image_dir.glob('*.png')) + list(image_dir.glob('*.PNG'))
    if not png_files:
        print(f"Error: No PNG files found in {image_dir}")
        return None

    print(f"Found {len(png_files)} PNG files")

    # Create filesystem with same config as C code in pico_hal.c
    fs = LittleFS(
        block_size=BLOCK_SIZE,
        block_count=BLOCK_COUNT,
        read_size=READ_SIZE,
        prog_size=PROG_SIZE,
        lookahead_size=LOOKAHEAD_SIZE
    )

    # Create pics directory (without leading slash for LittleFS compatibility)
    try:
        fs.mkdir('pics')
    except:
        pass  # Directory might already exist

    # Copy files
    total_size = 0
    for png_file in sorted(png_files):
        filename = png_file.name
        fs_path = f'pics/{filename}'

        with open(png_file, 'rb') as f:
            data = f.read()

        with fs.open(fs_path, 'wb') as f:
            f.write(data)

        print(f"  Added: {filename} ({len(data)} bytes)")
        total_size += len(data)

    print(f"Total: {total_size} bytes in filesystem")

    # Get the filesystem image
    fs_image = bytes(fs.context.buffer)

    # Write raw filesystem image
    raw_file = output_file.replace('.uf2', '.bin')
    with open(raw_file, 'wb') as f:
        f.write(fs_image)
    print(f"Wrote raw filesystem: {raw_file} ({len(fs_image)} bytes)")

    # Convert to UF2
    fs_addr = FLASH_BASE + FS_OFFSET
    uf2_data = binary_to_uf2(fs_image, fs_addr)

    with open(output_file, 'wb') as f:
        f.write(uf2_data)
    print(f"Wrote UF2 filesystem: {output_file} ({len(uf2_data)} bytes)")

    return uf2_data


def main():
    parser = argparse.ArgumentParser(description='Build LittleFS filesystem for Tufty 2040')
    parser.add_argument('image_dir', help='Directory containing PNG images')
    parser.add_argument('--firmware', '-f', help='Firmware UF2 file to combine')
    parser.add_argument('--output', '-o', default='filesystem.uf2', help='Output UF2 file')

    args = parser.parse_args()

    print(f"Building filesystem from: {args.image_dir}")
    print(f"Flash config: {FLASH_SIZE//1024//1024}MB total, {FS_SIZE//1024//1024}MB filesystem at offset 0x{FS_OFFSET:X}")

    # Build filesystem
    fs_uf2 = build_filesystem(args.image_dir, args.output)
    if fs_uf2 is None:
        sys.exit(1)

    # Combine with firmware if specified
    if args.firmware:
        if not os.path.exists(args.firmware):
            print(f"Error: Firmware file {args.firmware} not found")
            sys.exit(1)

        with open(args.firmware, 'rb') as f:
            fw_uf2 = f.read()

        combined = combine_uf2_files([fw_uf2, fs_uf2])
        combined_file = args.output.replace('.uf2', '_combined.uf2')

        with open(combined_file, 'wb') as f:
            f.write(combined)

        print(f"Wrote combined firmware + filesystem: {combined_file} ({len(combined)} bytes)")
        print(f"\nTo flash: copy {combined_file} to RPI-RP2 drive")
    else:
        print(f"\nTo flash filesystem only: copy {args.output} to RPI-RP2 drive")
        print("(Flash firmware first, then filesystem)")


if __name__ == '__main__':
    main()
