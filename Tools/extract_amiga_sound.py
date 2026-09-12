#!/usr/bin/env python3
"""Extract Paula loops from the already-decoded Amiga sound bank (file 01).

Offsets are sub0.asm inison: period table, echn, echcr, echac, echbb.
Bytes are XOR 0x80 exactly as the original copy loop (bchg #7).
Does not execute the game.
"""
from __future__ import annotations
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = Path("/Users/david/Documents/Codex/2026-09-05/su/work/reference/vroom-amiga-art/decoded/file01_block000.bin")
DST = ROOT / "Content/Data/amiga_sound.bin"


def main():
    bank = SRC.read_bytes()
    if len(bank) != 17104:
        raise SystemExit(f"unexpected sound bank size {len(bank)}")
    periods = bank[0:412]
    engine = bytes(b ^ 0x80 for b in bank[912:912 + 1166])
    screech = bytes(b ^ 0x80 for b in bank[2112:2112 + 2882])
    crash = bytes(b ^ 0x80 for b in bank[6712:6712 + 7736])
    noise = bytes(b ^ 0x80 for b in bank[14448:14448 + 2654])
    header = b"VROMSND1" + struct.pack("<HHHHH", 206, len(engine), len(screech), len(crash), len(noise))
    # periods stored little-endian u16 for the Mac mixer
    per_le = b"".join(struct.pack("<H", struct.unpack(">H", periods[i:i + 2])[0]) for i in range(0, 412, 2))
    DST.parent.mkdir(parents=True, exist_ok=True)
    DST.write_bytes(header + per_le + engine + screech + crash + noise)
    print(f"wrote {DST} ({DST.stat().st_size} bytes)")


if __name__ == "__main__":
    main()
