"""Decode only the seven Vroom Amiga cockpit poses from a local preserved disk.

Requires Pillow. Does not download, execute, or copy the game into the project.
See docs/AMIGA_ART.md for provenance, original offsets, palette and mask.
"""

import argparse
import hashlib
import json
from pathlib import Path
import struct
import zipfile

from PIL import Image


ARCHIVE_SHA256 = "d8155a4fac27a14d292682a318875bfa85a318292fed14ec116d09c246b5d6c5"
DISK_SHA256 = {
    "d0636fc8446a8909f04ed5ec54190eb386954098617c604ce2732bdc0deb9048",
    "953f0dc276242f9b8e2d1f363bb6e507602d8a680560dc3f516f0bbc703f884e",
}
COCKPIT_SHA256 = "a77a7d8875277b6efc9af470d54c307b8b1a6075c23d0920c92b5c9aa768b891"
POSE_ELEMENTS = (0, 1, 2, 6, 5, 4, 7)
POSE_NAMES = (
    "left_full", "left_medium", "left_small", "neutral",
    "right_small", "right_medium", "right_full",
)
STEERING_RANGES = ((-271, -168), (-167, -56), (-55, -8), (-7, 8), (9, 56), (57, 168), (169, 271))


def digest(data):
    return hashlib.sha256(data).hexdigest()


def read_disk(path):
    data = path.read_bytes()
    if zipfile.is_zipfile(path):
        if digest(data) != ARCHIVE_SHA256:
            raise ValueError("Archive differs from the documented preserved edition")
        with zipfile.ZipFile(path) as archive:
            data = archive.read("Version 2/Vroom/Disk.1")
    if digest(data) not in DISK_SHA256:
        raise ValueError("Disk differs from either documented Amiga edition")
    return data


def unescape(data):
    output = bytearray()
    cursor = 0
    while cursor < len(data):
        if data[cursor] == 7:
            cursor += 1
        if cursor < len(data):
            output.append(data[cursor])
            cursor += 1
    return bytes(output)


def decompress_cockpit(disk):
    # Track0 has a different boot format and contains no requested graphic data.
    stream = bytes(0x16D4) + b"".join(
        disk[0x1600 + (track - 1) * 0x16DC + 8:0x1600 + track * 0x16DC]
        for track in range(1, 158)
    )
    start = struct.unpack_from(">I", disk, 0x1D38 + 8 * 4)[0]
    header = unescape(stream[start:start + 1000])
    if header[0] != 1:
        raise ValueError("Expected Huffman header")
    escape, cursor = header[1], 2
    blocks = []
    while header[cursor:cursor + 4] != b"\xff" * 4:
        blocks.append(struct.unpack_from(">HH", header, cursor))
        cursor += 4
    if len(blocks) != 1:
        raise ValueError("Expected a single cockpit block")
    cursor += 5
    info = header[cursor]
    cursor += 1
    field_bits, minimum = info >> 3, info & 7
    encoded_lengths = int.from_bytes(header[cursor:cursor + field_bits * 32], "big")
    cursor += field_bits * 32
    lengths = [
        (encoded_lengths >> ((255 - symbol) * field_bits)) & ((1 << field_bits) - 1)
        for symbol in range(256)
    ]
    lengths = [value + minimum - 1 if value else 0 for value in lengths]
    # Original canonical ordering: shortest first, descending symbol for ties;
    # assign codes backwards, from zero at the longest bit length.
    ordered = sorted((length, -symbol) for symbol, length in enumerate(lengths) if length)
    code, length, codes = 0, ordered[-1][0], {}
    for next_length, negative_symbol in reversed(ordered):
        code >>= length - next_length
        length = next_length
        codes[length, code] = -negative_symbol
        code += 1
    data_offset = struct.unpack_from(">H", header, cursor)[0]
    output_size, compressed_size = blocks[0]
    compressed = unescape(stream[start + data_offset:start + data_offset + compressed_size])
    bit_cursor = 0

    def symbol():
        nonlocal bit_cursor
        code = 0
        for length in range(1, max(lengths) + 1):
            code = (code << 1) | ((compressed[bit_cursor // 8] >> (7 - bit_cursor % 8)) & 1)
            bit_cursor += 1
            if (length, code) in codes:
                return codes[length, code]
        raise ValueError("Invalid Huffman code")

    output = bytearray()
    while len(output) < output_size:
        value = symbol()
        if value != escape:
            output.append(value)
        else:
            count = symbol()
            value = escape if count < 2 else symbol()
            output.extend(bytes([value]) * (count + 1))
    if len(output) != output_size or digest(output) != COCKPIT_SHA256:
        raise ValueError("Cockpit decompression does not match the verified original bytes")
    return bytes(output)


def nibbles(data):
    return [nibble for byte in data for nibble in (byte >> 4, byte & 15)]


def extract_poses(data, output):
    if data[:2] != b"\x00\x0c":
        raise ValueError("Expected the twelve-element ITCR graphic file")
    words = list(struct.unpack_from(">16H", data, 2))
    palette = [tuple(round(((word >> shift) & 7) * 255 / 7) for shift in (8, 4, 0)) for word in words]
    offset, selected = 34, {}
    for element in range(data[1]):
        color_size, run_size, x, y, method, flags, width, height = struct.unpack_from(">HHhhBBHH", data, offset)
        width, height = width + 1, height + 1
        if element in POSE_ELEMENTS:
            if (method, flags, width, height) != (1, 0, 96, 28):
                raise ValueError("Unexpected cockpit graphic traversal")
            colors = nibbles(data[offset + 14:offset + 14 + color_size])
            run_offset = offset + 14 + color_size + ((color_size + run_size) & 1)
            counts = nibbles(data[run_offset:run_offset + run_size])
            pixels, count, color = [], 0, 0
            for nibble in counts:
                count += nibble
                if nibble != 15:
                    if color >= len(colors):
                        break
                    # tabins2 writes encoded bit0 to bitplane3, etc.
                    index = int(f"{colors[color]:04b}"[::-1], 2)
                    pixels.extend([index] * count)
                    count, color = 0, color + 1
            if len(pixels) < width * height:
                raise ValueError("Truncated cockpit pixels")
            image = Image.new("RGBA", (width, height))
            for row in range(height):
                for column in range(width):
                    source_x = column if row % 2 == 0 else width - 1 - column
                    index = pixels[row * width + source_x]
                    rgba = (*palette[index], 255) if index else (0, 0, 0, 0)
                    image.putpixel((column, row), rgba)
            selected[element] = (image, {"x": x, "y": y, "width": width, "height": height})
        offset += (color_size + run_size + 15) & ~1
    output.mkdir(parents=True, exist_ok=True)
    poses = []
    for pose, element in enumerate(POSE_ELEMENTS):
        image, rectangle = selected[element]
        filename = f"cockpit_pose_{pose:02d}.png"
        destination = output / filename
        image.save(destination)
        poses.append({
            "pose": pose, "direction": POSE_NAMES[pose], "source_element": element,
            "steering_integer_range": STEERING_RANGES[pose],
            "file": filename, "texture": f"/Game/DeveloperReference/T_AmigaCockpit{pose:02d}",
            "sheet_rectangle": rectangle, "sha256": digest(destination.read_bytes()),
        })
    manifest = {
        "source_archive_sha256": ARCHIVE_SHA256,
        "decoded_cockpit_sha256": COCKPIT_SHA256,
        "palette_words": words, "palette_rgb_normalized": palette,
        "palette_conversion": "round(channel_3bit * 255 / 7); encoded pixel bits reversed",
        "alpha": "Native index0 transparent: NOT(plane0 OR plane1 OR plane2 OR plane3)",
        "screen": {"width": 320, "height": 200},
        "display_rectangle": {"x": 112, "y": 172, "width": 96, "height": 28},
        "instrument_rectangles_local": {
            "gear": {"x": 32, "y": 16, "width": 16, "height": 6},
            "speed": {"x": 52, "y": 16, "width": 12, "height": 5},
            "rpm": {"x": 32, "y": 25, "width": 32, "height": 2},
        },
        "poses": poses,
    }
    (output / "cockpit.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path, required=True, help="Local jeu32-amiga.zip or verified WHDLoad Disk.1")
    parser.add_argument("--output", type=Path, default=Path(__file__).resolve().parents[2] / "docs/reference/amiga")
    args = parser.parse_args()
    extract_poses(decompress_cockpit(read_disk(args.input)), args.output)
    print(f"Exported seven original 96x28 cockpit poses to {args.output}")


if __name__ == "__main__":
    main()
