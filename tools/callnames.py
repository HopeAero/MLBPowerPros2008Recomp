# SPDX-License-Identifier: GPL-3.0-or-later
"""Callname helpers for MLB Power Pros 2008 (docs/ingenieria-inversa.md, "Bancos de voz y nombres").

  callnames.py decode <voiceN.bin> <VoiceId> <out.wav>
      Decode one player's name from a voice bank to a WAV, for listening (local files only).
  callnames.py set-voice <game_data.pack> <surname> <VoiceId> <out.pack>
      Copy of the roster pack with every player of that surname set to VoiceId (local test mods).

Banks 14, 15 and 17 hold the names: local index = VoiceId - 0x800.
"""
import struct
import sys
import wave
from pathlib import Path

SECTOR = 0x800
VOICE_BIT, VOICE_BITS = 0x92 * 8 + 2, 14  # VoiceId in a 0xB0-byte player record


def decode_adpcm(data, coefs, samples):
    """DSP-ADPCM frames (8 bytes: header + 14 nibbles) to `samples` PCM values."""
    out, h1, h2 = [], 0, 0
    for frame in range(0, len(data), 8):
        ps = data[frame]
        scale = 1 << (ps & 15)
        c1, c2 = coefs[(ps >> 4) * 2], coefs[(ps >> 4) * 2 + 1]
        for byte in data[frame + 1:frame + 8]:
            for nibble in (byte >> 4, byte & 15):
                if len(out) == samples:
                    return out
                if nibble >= 8:
                    nibble -= 16
                value = (nibble * scale * 2048 + c1 * h1 + c2 * h2 + 1024) >> 11
                value = max(-32768, min(32767, value))
                h2, h1 = h1, value
                out.append(value)
    return out


def decode_phrase(bank_path, local):
    """(PCM samples, rate) of phrase `local` of a voiceN.bin."""
    with open(bank_path, "rb") as f:
        header = f.read(24)
        index_sector, count = struct.unpack_from(">II", header, 4)
        if not 0 <= local < count:
            raise ValueError(f"phrase {local} outside 0..{count - 1}")
        f.seek(index_sector * SECTOR + local * 2)
        sector = struct.unpack(">H", f.read(2))[0]
        f.seek(sector * SECTOR)
        phrase = f.read(SECTOR)
        size = struct.unpack_from(">I", phrase, 0)[0]
        rate = struct.unpack_from(">I", phrase, 4)[0] & 0xFFFF
        nibbles = struct.unpack_from(">I", phrase, 0x88)[0]
        coefs = struct.unpack_from(">16h", phrase, 0x90)
        data = f.read(size)
    return decode_adpcm(data, coefs, nibbles * 14 // 16), rate


def bits_at(record, bit, count):
    value = int.from_bytes(record, "big")
    return (value >> (len(record) * 8 - bit - count)) & ((1 << count) - 1)


def set_bits(record, bit, count, value):
    whole = int.from_bytes(record, "big")
    shift = len(record) * 8 - bit - count
    whole = whole & ~(((1 << count) - 1) << shift) | (value << shift)
    record[:] = whole.to_bytes(len(record), "big")


def write_wav(path, samples, rate):
    with wave.open(str(path), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(struct.pack(f"<{len(samples)}h", *samples))


def set_voice(pack_path, surname, voice, out_path):
    sys.path.insert(0, str(Path(__file__).resolve().parent / "roster"))
    import align_2007_2008 as roster
    charmap = roster.load_charmap(Path(__file__).resolve().parents[1] / "local/vendor/PowerUpPlus")
    pack = bytearray(Path(pack_path).read_bytes())
    changed = 0
    for i in range(1500):
        start = roster.PACK_PLAYERS_2008 + i * roster.PLAYER
        record = bytearray(pack[start:start + roster.PLAYER])
        last = "".join(charmap.get(struct.unpack_from(">H", record, 0x16 + 2 * k)[0], "") for k in range(14)).strip()
        if struct.unpack_from(">H", record, 0)[0] and last == surname:
            set_bits(record, VOICE_BIT, VOICE_BITS, voice)
            pack[start:start + roster.PLAYER] = record
            changed += 1
    Path(out_path).write_bytes(pack)
    return changed


def main(argv):
    if len(argv) == 4 and argv[0] == "decode":
        samples, rate = decode_phrase(argv[1], int(argv[2]) - 0x800)
        write_wav(argv[3], samples, rate)
        print(f"{argv[3]}: {len(samples)} samples at {rate} Hz")
    elif len(argv) == 5 and argv[0] == "set-voice":
        print(f"{set_voice(argv[1], argv[2], int(argv[3]), argv[4])} player(s) set to voice {argv[3]}")
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main(sys.argv[1:])
