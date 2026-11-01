# SPDX-License-Identifier: GPL-3.0-or-later
"""Self-checks for tools/callnames.py (synthetic data only, no game files)."""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import callnames


def test_decode_adpcm_known_frame():
    # Header 0x00: coefficient pair 0 (0, 0), scale 1 << 0; nibbles 1..7 then -8..6 (two's complement).
    data = bytes([0x00, 0x12, 0x34, 0x56, 0x7F, 0xED, 0xCB, 0xA9])
    samples = callnames.decode_adpcm(data, (0,) * 16, 14)
    assert samples == [1, 2, 3, 4, 5, 6, 7, -1, -2, -3, -4, -5, -6, -7], samples


def test_decode_adpcm_stops_at_sample_count():
    data = bytes([0x00, 0x12, 0x34, 0x56, 0x7F, 0xED, 0xCB, 0xA9])
    assert callnames.decode_adpcm(data, (0,) * 16, 3) == [1, 2, 3]


def test_set_voice_bits():
    record = bytearray(0xB0)
    callnames.set_bits(record, 0x92 * 8 + 2, 14, 7300)
    assert callnames.bits_at(record, 0x92 * 8 + 2, 14) == 7300
    record_clear = bytearray(record)
    callnames.set_bits(record_clear, 0x92 * 8 + 2, 14, 0)
    assert record_clear == bytearray(0xB0)


if __name__ == "__main__":
    for name, test in list(globals().items()):
        if name.startswith("test_"):
            test()
            print("ok", name)
