# SPDX-License-Identifier: GPL-3.0-or-later
"""Callname voices (VoiceId 7223-10239, docs/superpowers/specs/2026-10-31-callnames-design.md) pass through
the 2007 <-> 2008 voice conversion untouched; the game's own voices still move with VOICE_RUNS."""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import powerup_to_2008 as conv


def test_callname_voices_pass_through():
    for voice in (7223, 7300, 10239):
        assert conv.voice_2008(voice) == voice, voice
        # Same number in PowerUp's 2007 numbering: ONLY_2008_VOICE + voice would not fit the 14-bit field.
        assert conv.voice_2007(voice) == voice, voice


def test_game_voices_unchanged():
    first, last, offset = conv.VOICE_RUNS[0]
    assert conv.voice_2008(first) == first + offset
    assert conv.voice_2008(7222) == 0
    assert conv.voice_2008(10240) == 0


if __name__ == "__main__":
    for name, test in list(globals().items()):
        if name.startswith("test_"):
            test()
            print("ok", name)
