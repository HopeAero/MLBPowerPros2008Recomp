"""Convert a PowerUp / PowerUp+ roster (MLB Power Pros 2007 Wii save, pm2maus.dat) into an MLB Power
Pros 2008 roster mod.

2008 keeps the 2007 save's roster block inside project.bin's game_data.pack, at 0x20: 1500 player
records of 0xB0 bytes, then lineups, free agents and 32 teams of 0x140, 0x43E80 bytes in all (the
2007 save holds the same block at 0x68C74). Player records are rebuilt field by field, because 2008
moved part of the record (see tools/roster/align_2007_2008.py and docs/roster-2008.md):
  - up to 0x5C and 0x60..0x6F (names, number, appearance, fielding capabilities, hot zones,
    batting side, ratings) are unchanged, except the face: 2008 inserted faces, so 2007's ids move
    by 0 to 12 (face_2008), and skin/eyes, where 2008 added a complexion (skin_2008);
  - batting and pitching forms grew to 9 bits, which moves position and the P/C capabilities by
    2 bits;
  - the second "hitting versus lefty" field is gone, so 0x70..0x77.6 moves back 3 bits; 2008 adds
    a bit at 0x77.4, so from 0x77.7 (pivot man, pull hitter, pitching block) it is back 2 bits;
  - birth date moves back 12 bits; years in the majors and season stats are at 0x85.2 (years),
    0x85.7 (AVG), 0x88.0 (RBI), 0x89.2 (HR); ERA stays at 0x8C.0;
  - 0x8C..0x93 and the pitch arsenal (0x94..) are unchanged; the voice id keeps its place but
    2008 renumbered the voices: a 2007 voice id moves with VOICE_RUNS, and one 2008 removed becomes 0.
Bits PowerUp has no name for travel with their zone, so nothing the game stores is dropped.

A player who is also in the original 2008 roster (same name and birthday; when those repeat, as with
2008's two "Great Harris", the same id decides) starts from his 2008 record: what only 2008 has (his
voice, the bits 2008 added) survives, and so do his 2008 values for the bits PowerUp has no name for
(PowerUp cannot edit them and fills them from whatever player had that slot in its base save) and
the bytes after a long name's special id (u16 >= 0xF00 at 0x02). Everyone else starts from zeros;
unused slots get 2008's empty record. Lineups, free agents and teams come from the save, with 2008's
empty entries and the entry bits PowerUp does not model (convert_rest).

A mod whose mod.toml says roster_ids = "2008" (PowerUp+'s 2008 export) has 2008 voice, face and
skin/eyes values in its .dat already, so they are copied as they are.

The output is a mod folder: <out>/mod.toml and <out>/project/game_data.pack, built on the player's
own game_data.pack (extract it with tools/projectbin.py), so nothing from the game ships.

usage: powerup_to_2008.py <powerup save .dat> <original 2008 game_data.pack> <out mod dir> [name]
"""
import re
import sys
from pathlib import Path

PLAYER = 0xB0
SLOTS = 1500
BLOCK = 0x43E80
SAVE_BLOCK_2007 = 0x68C74
PACK_BLOCK_2008 = 0x20
NAMES = slice(0x16, 0x4E)                # last and first name
# The rest of the block, after the players: 32 lineups of 0x48 (18 entries of 4 bytes: id, position),
# 200 free agents of 8 bytes, 32 teams of 0x140 (40 entries of 8 bytes).
LINEUPS, FREE_AGENTS, TEAMS = 0x40740, 0x41040, 0x41680
# Bits of a team entry that PowerUp's GSTeamPlayerEntry has no field for.
TEAM_ENTRY_UNMODELED = (0x00, 0x00, 0x38, 0x3F, 0xFF, 0xF0, 0xE3, 0x00)

# PowerUp GSPlayer fields: name -> (bit position in 2007, bits). Parsed from the PowerUp sources at
# runtime when available; this table only lists the fields whose 2008 position differs.
MOVED = {
    # name: (2008 bit position, 2008 width) or None when 2008 dropped the field
    "BattingForm": (0x2E4, 9),
    "PitchingForm": (0x2ED, 9),
    "PrimaryPosition": (0x2F6, 4),
    "PitcherCapability": (0x2FA, 3),
    "CatcherCapability": (0x2FD, 3),
    "HittingVersusLefty2": None,
    "BirthYear": (0x414, 11),
    "BirthMonth": (0x420, 4),
    "BirthDay": (0x424, 5),
    "YearsInMajors": (0x42A, 5),
    "BattingAveragePoints": (0x42F, 10),
    "RunsBattedIn": (0x440, 10),
    "HomeRuns": (0x44A, 10),
    "VoiceId": None,
    # Checked on 2008's player sheet: the bit PowerUp calls "barehand catch" is the game's Cannon
    # Arm, the game's Barehand Catch is a bit PowerUp has no name for, and PowerUp's "cannon arm" is
    # the high bit of a hidden +1/-1 trait the sheet never shows. PowerUp's own rosters use the
    # boxes by their names, so 2008 gets them by meaning; 2007 saves keep PowerUp's layout.
    "CanBarehandCatch": (0x3BD, 1),      # 2008 0x77.5 Barehand Catch
    "HasCannonArm": (0x3A4, 1),          # 2008 0x74.4 Cannon Arm
    "Bit_76_0": None,                    # low bit of the hidden trait: base-save leftovers in PowerUp
    "Bit_77_7": None,                    # where 2007 keeps Barehand Catch, which PowerUp cannot edit
}
# 2008's hidden +1/-1 trait (0x75.4, 2 bits): kept from the 2008 record, 0 for new players.
HIDDEN_2008 = [0x3AC, 0x3AD]
# Abilities only 2008 has, which PowerUp+ (GSPlayer *2008 fields) writes into 2007-layout slots the
# 2008 conversion leaves unused: (name, 2007 bit, bits, 2008 bit, bits). Only a mod with
# roster_ids = "2008" (PowerUp+'s 2008 export) carries them; other saves keep 2008's own values.
MOD_2008 = [
    ("HasGoodEye2008", 0x3BF, 1, 0x3BC, 1),             # Gd Eye (0x77.4, the bit 2008 inserted)
    ("GoodOrPoorNightGame2008", 0x3C6, 2, 0x3C4, 2),    # Gd/Pr Night Game (0x78.4)
]


def target(name, bit, bits):
    """2008 (bit, bits) for a 2007 field, or None to drop it."""
    if name in MOVED:
        return MOVED[name]
    if bit < 0x37D:
        return bit, bits                 # unchanged through HittingVersusLefty1
    if 0x380 <= bit < 0x3BF:
        return bit - 3, bits             # 0x70.0 .. 0x77.6
    if 0x3BF <= bit < 0x41B:
        return bit - 2, bits             # 0x77.7 .. PowerOrBreakingBallPitcher
    if bit >= 0x460:
        return bit, bits                 # ERA and the pitch arsenal
    return None


def load_fields(powerup_repo):
    src = (powerup_repo / "PowerUp/PowerUp/GameSave/Objects/Players/GSPlayer.cs").read_text(encoding="utf-8-sig")
    fields = []
    for m in re.finditer(r"\[GS(UInt|SInt|Boolean)\((0x[0-9a-fA-F]+)(?:, bits: (\d+))?(?:, bitOffset: (\d+))?"
                         r"[^\]]*\)\]\s*public [\w?<>]+ (\w+)", src):
        _, off, bits, bit_offset, name = m.groups()
        fields.append((name, int(off, 16) * 8 + int(bit_offset or 0), int(bits or 1)))
    # Unnamed bits in the zones that move as a whole (hidden abilities, flags) go as 1-bit fields.
    # 0x5C..0x5F (the 2008 forms grew over it) and the padding after the pitching block are left out.
    named = {b for _, bit, count in fields for b in range(bit, bit + count)}
    zones = [*range(0x6F * 8, 0x40B), *range(0x460, 0x94 * 8)]
    fields += [(f"Bit_{b // 8:X}_{b % 8}", b, 1) for b in zones if b not in named]
    return fields


def get_bits(record, bit, count):
    value = int.from_bytes(record, "big")
    return (value >> (len(record) * 8 - bit - count)) & ((1 << count) - 1)


def set_bits(record, bit, count, field):
    value = int.from_bytes(record, "big")
    shift = len(record) * 8 - bit - count
    mask = ((1 << count) - 1) << shift
    value = (value & ~mask) | ((field << shift) & mask)
    record[:] = value.to_bytes(len(record), "big")


def person_2008(record):
    return bytes(record[NAMES]), get_bits(record, 0x420, 4), get_bits(record, 0x424, 5)


def person_2007(record):
    return bytes(record[NAMES]), get_bits(record, 0x42B, 4), get_bits(record, 0x42F, 5)


def originals_by_name(block):
    """Player records of a 2008 roster block by name and birthday (a list: they can repeat)."""
    found = {}
    for i in range(SLOTS):
        record = block[i * PLAYER:(i + 1) * PLAYER]
        if record[0] or record[1]:
            found.setdefault(person_2008(record), []).append(bytes(record))
    return found


def convert_rest(old, original):
    """Lineups, free agents and teams of a PowerUp save (`old`, from 0x40740 of the block) over the
    original 2008 ones. Entries PowerUp leaves blank get 2008's empty entry, used free agents 2008's
    filler bytes; a team entry for the same member keeps the original's bits PowerUp does not model,
    and a lineup entry with the same player and position keeps the original's flags."""
    rest = bytearray(old)
    def entries(start, count, size):
        return [start - LINEUPS + i * size for i in range(count)]
    fa = entries(FREE_AGENTS, 200, 8)
    teams = entries(TEAMS, 32 * 40, 8)
    empty = next((original[o:o + 8] for o in fa + teams if original[o:o + 2] == b"\0\0"), None)
    used_fa = next((original[o:o + 8] for o in fa if original[o:o + 2] != b"\0\0"), None)
    for o in fa + teams:
        if rest[o:o + 2] == b"\0\0" and empty:
            rest[o:o + 8] = empty
        elif o < TEAMS - LINEUPS and used_fa:
            rest[o + 2:o + 8] = used_fa[2:]
        elif o >= TEAMS - LINEUPS and rest[o:o + 2] == original[o:o + 2]:
            for k, mask in enumerate(TEAM_ENTRY_UNMODELED):
                rest[o + k] = rest[o + k] & ~mask | original[o + k] & mask
    for o in entries(LINEUPS, 32 * 18, 4):
        if rest[o:o + 2] == original[o:o + 2] and rest[o + 2] >> 4 == original[o + 2] >> 4:
            rest[o:o + 4] = original[o:o + 4]
    return bytes(rest)


def unnamed_2008(fields):
    """2008 bit positions that hold bits PowerUp has no name for."""
    moved = [target(name, bit, 1) for name, bit, _ in fields if name.startswith("Bit_")]
    return unnamed_copied(fields) + [place[0] for place in moved if place] + HIDDEN_2008


def unnamed_2007(fields):
    """The same bits at their 2007 positions, as a PowerUp save holds them."""
    mod = {b for _, frm, bits, _, _ in MOD_2008 for b in range(frm, frm + bits)}
    return unnamed_copied(fields) + [bit for name, bit, _ in fields
                                     if name.startswith("Bit_") and target(name, bit, 1) and bit not in mod]


def unnamed_copied(fields):
    named = {b for name, bit, count in fields if not name.startswith("Bit_") for b in range(bit, bit + count)}
    # 0x4E..0x5B: after the names (0x02..0x4D, strings PowerUp's regex above does not list).
    return [b for b in [*range(0x4E * 8, 0x5C * 8), *range(0x60 * 8, 0x6F * 8)] if b not in named]


VOICE = (0x492, 14)                      # VoiceId, same place in both games
FACE = (0x2A4, 8)

# 2007 voice id runs (first, last, offset to 2008's id): 2008 kept the order but inserted and removed
# voices. From align_2007_2008.py --voices (810 of the 968 voices PowerUp knows; held-out anchors all
# land on their real 2008 id).
VOICE_RUNS = [
    (2051, 2055, 0), (2056, 2059, 1), (2061, 2064, 2), (2066, 2066, 4), (2067, 2069, 5), (2070, 2083, 7),
    (2090, 2092, 5), (2096, 2101, 4), (2102, 2108, 5), (2110, 2112, 5), (2115, 2119, 4), (2120, 2139, 5),
    (2140, 2142, 7), (2144, 2147, 6), (2148, 2152, 8), (2154, 2173, 9), (2175, 2181, 10), (2184, 2186, 12),
    (2189, 2197, 13), (2198, 2199, 14), (2200, 2208, 15), (2210, 2212, 16), (2213, 2216, 17), (2219, 2223, 15),
    (2224, 2232, 17), (2234, 2240, 16), (2244, 2244, 15), (2248, 2250, 16), (2251, 2258, 17), (2259, 2265, 18),
    (2266, 2268, 19), (2269, 2270, 20), (2272, 2272, 21), (2274, 2277, 23), (2279, 2279, 24), (2282, 2294, 23),
    (2295, 2298, 24), (2299, 2304, 25), (2305, 2320, 26), (2322, 2325, 27), (2327, 2334, 29), (2338, 2339, 29),
    (2341, 2345, 28), (2347, 2349, 27), (2351, 2355, 26), (2357, 2372, 27), (2374, 2375, 29), (2377, 2379, 28),
    (2381, 2381, 29), (2382, 2382, 30), (2385, 2386, 31), (2387, 2387, 32), (2388, 2388, 33), (2390, 2395, 34),
    (2397, 2403, 35), (2405, 2407, 36), (2408, 2411, 37), (2412, 2417, 38), (2418, 2424, 39), (2426, 2430, 41),
    (2431, 2433, 42), (2434, 2437, 46), (2438, 2442, 48), (2443, 2444, 49), (2446, 2446, 50), (2447, 2451, 51),
    (2452, 2458, 52), (2459, 2459, 53), (2460, 2460, 54), (2462, 2475, 55), (2477, 2479, 57), (2480, 2480, 58),
    (2482, 2483, 59), (2486, 2487, 60), (2488, 2490, 61), (2492, 2494, 63), (2496, 2497, 62), (2499, 2503, 61),
    (2504, 2510, 62), (2512, 2512, 63), (2515, 2515, 62), (2516, 2516, 64), (2518, 2518, 63), (2519, 2525, 64),
    (2526, 2529, 67), (2530, 2530, 68), (2532, 2533, 70), (2535, 2538, 69), (2539, 2544, 70), (2545, 2545, 71),
    (2547, 2548, 72), (2549, 2551, 73), (2552, 2552, 74), (2554, 2557, 75), (2558, 2584, 76), (2585, 2588, 77),
    (2589, 2590, 78), (2591, 2592, 79), (2594, 2594, 80), (2595, 2595, 81), (2597, 2597, 83), (2598, 2598, 84),
    (2600, 2611, 85), (2612, 2614, 86), (2615, 2615, 87), (2616, 2616, 89), (2619, 2620, 88), (2621, 2629, 89),
    (2630, 2634, 90), (2636, 2637, 92), (2639, 2657, 93), (2661, 2661, 91), (2663, 2664, 90), (2665, 2665, 91),
    (2668, 2675, 90), (2676, 2687, 91), (2688, 2688, 93), (2689, 2701, 94), (2703, 2717, 95), (2719, 2722, 94),
    (2723, 2726, 95), (2727, 2732, 96), (2733, 2737, 99), (2739, 2764, 102), (2765, 2766, 103), (2767, 2772, 104),
    (2774, 2774, 106), (2775, 2783, 107), (2785, 2788, 109), (2789, 2792, 110), (2794, 2795, 113), (2797, 2797, 116),
    (2799, 2799, 118), (2800, 2800, 119), (2801, 2801, 121), (2803, 2803, 122), (2806, 2809, 123), (2810, 2810, 126),
    (2812, 2815, 127), (2816, 2819, 130), (2820, 2821, 133), (2822, 2822, 139), (2823, 2825, 141), (2828, 2830, 140),
    (2831, 2831, 142), (2833, 2833, 143), (2834, 2837, 144), (2838, 2838, 146), (2842, 2845, 150), (2846, 2848, 151),
    (2849, 2849, 152), (2855, 2861, 154), (2863, 2867, 155), (2869, 2870, 157), (2871, 2872, 158), (2874, 2874, 159),
    (2875, 2875, 160), (2877, 2877, 164), (2880, 2881, 168), (2882, 2883, 169), (2884, 2884, 170), (2885, 2885, 171),
    (2887, 2887, 173), (2889, 2889, 174), (2890, 2890, 175), (2891, 2891, 176), (2893, 2893, 178), (2895, 2898, 177),
    (2899, 2902, 181), (2905, 2909, 186), (2913, 2913, 187), (2916, 2916, 189), (2919, 2921, 190), (2924, 2924, 191),
    (2926, 2926, 193), (2927, 2931, 194), (2933, 2935, 195), (2940, 2941, 200), (2944, 2944, 202), (2949, 2951, 203),
    (2952, 2955, 206), (2957, 2958, 207), (2959, 2959, 208), (2961, 2961, 210), (2962, 2962, 211), (2963, 2963, 216),
    (2964, 2966, 217), (2968, 2968, 219), (2969, 2969, 220), (2972, 2974, 219), (2977, 2977, 221), (2978, 2978, 224),
    (2979, 2979, 225), (2983, 2983, 227), (2984, 2985, 228), (2986, 2987, 230), (2990, 2991, 233), (2992, 2993, 234),
    (2994, 2996, 236), (2998, 3001, 235), (3003, 3007, 234), (3818, 3821, 650), (3825, 3825, 649), (3828, 3829, 647),
    (3831, 3831, 646), (4081, 4081, 708),
]


CALLNAME_VOICES = range(7223, 10240)    # new callnames from mods (tools/callnames.py): same id in both games


def voice_2008(voice):
    """2007 voice id -> 2008's, or 0 (no voice) for one 2008 does not have."""
    if voice in CALLNAME_VOICES:
        return voice
    return next((voice + d for first, last, d in VOICE_RUNS if first <= voice <= last), 0)


ONLY_2008_VOICE = 10000                  # PowerUp+ numbers a voice only 2008 has as this + its 2008 id


def voice_2007(voice):
    """2008 voice id -> 2007's (the runs never overlap in 2008), or ONLY_2008_VOICE + voice."""
    if voice == 0 or voice in CALLNAME_VOICES:
        return voice
    return next((voice - d for first, last, d in VOICE_RUNS if first <= voice - d <= last), ONLY_2008_VOICE + voice)


def face_2008(face):
    """2007 face id -> 2008's. 2008 inserted faces after 27, 105, 160 and 176 (707 of the 729 players
    in both games match with this; the rest got another face)."""
    return face + (0 if face < 28 else 1 if face < 106 else 2 if face < 161 else 7 if face < 177 else 12)


SKIN = (0x2AC, 4)                        # SkinAndEyes: complexion, + offset for brown eyes


def skin_2008(value):
    """2007 SkinAndEyes -> 2008's. 2007 has 5 complexions and adds 5 for brown eyes; 2008 inserted a
    complexion at 2 and adds 6 (695 of the 729 common players match; 272 without it)."""
    return value + (0 if value < 2 else 1 if value < 7 else 2)


def skin_2007(value):
    """2008 SkinAndEyes -> 2007's; 2008's new complexion becomes the one before it."""
    return max(v for v in range(16) if skin_2008(v) <= value)


def face_2007(face):
    """2008 face id -> 2007's; a face 2008 added becomes the 2007 one before it."""
    return max(f for f in range(256) if face_2008(f) <= face)


def empty_record(block):
    """2008 fills unused slots with a fixed record (zeros plus "no value" stats), not zeros."""
    for i in range(SLOTS):
        record = block[i * PLAYER:(i + 1) * PLAYER]
        if record[0] == 0 and record[1] == 0:
            return bytes(record)
    return bytes(PLAYER)


def convert_player(old, fields, originals=None, ids_2008=False, empty=bytes(PLAYER), typical=None, native=True):
    if old[0] == 0 and old[1] == 0:
        return empty
    same = (originals or {}).get(person_2007(old), [])
    # Repeated name and birthday (2008 has two "Great Harris"): the one with the same id.
    base = next((o for o in same if o[:2] == old[:2]), same[0] if same else None)
    new = bytearray(base or bytes(PLAYER))
    new[:0x5C] = old[:0x5C]              # id, names, flags, team, number, appearance
    new[0x60:0x6F] = old[0x60:0x6F]      # capabilities, hot zones, side, ratings
    for name, bit, count in fields:
        if bit < 0x5C * 8 or 0x60 * 8 <= bit < 0x6F * 8:
            continue
        place = target(name, bit, count)
        if place is None:
            continue
        value = get_bits(old, bit, count)
        if name == "BirthYear" and value:
            # PowerUp stores "opening day 2007 minus age"; 2008's calendar is a year later.
            value += 1
        set_bits(new, place[0], place[1], value)
    if ids_2008:
        set_bits(new, *VOICE, get_bits(old, *VOICE))
    else:
        set_bits(new, *FACE, face_2008(get_bits(old, *FACE)))
        set_bits(new, *SKIN, skin_2008(get_bits(old, *SKIN)))
        if not base:                     # a player 2008 also has keeps 2008's own voice
            set_bits(new, *VOICE, voice_2008(get_bits(old, *VOICE)))
    if base:
        for b in unnamed_2008(fields):
            set_bits(new, b, 1, get_bits(base, b, 1))
        if new[2:4] == base[2:4] and int.from_bytes(base[2:4], "big") >= 0xF00:
            new[4:0x16] = base[4:0x16]
    elif ids_2008 and typical:
        # PowerUp+'s 2008 export starts from a zeroed block: its new players take 2008's usual value
        # of each bit PowerUp cannot name (0x4E-0x4F and 0x92.0 hold the lineup screen's names).
        for b, value in typical.items():
            set_bits(new, b, 1, value)
    if ids_2008:
        for _, frm, bits, to, to_bits in MOD_2008:
            set_bits(new, to, to_bits, get_bits(old, frm, bits))
    if native:
        set_bits(new, 0x283, 2, 0b10)    # Unedited (0x50.3) set, IsEdited (0x50.4) clear, as in 2008
    return bytes(new)


def typical_unnamed(block, fields):
    """For each unnamed bit, the value most of 2008's players have."""
    records = [block[i * PLAYER:(i + 1) * PLAYER] for i in range(SLOTS)]
    records = [r for r in records if r[0] or r[1]]
    return {b: int(sum(get_bits(r, b, 1) for r in records) * 2 > len(records)) for b in unnamed_2008(fields)}


def revert_player(new, fields, base=None):
    """2008 record back to the 2007 layout (PowerUp+ imports the 2008 roster this way). Face and voice
    go back to 2007's numbering, which PowerUp's editor shows (a voice only 2008 has becomes
    ONLY_2008_VOICE + its 2008 id); the second "versus lefty", which 2008 dropped, comes from `base`,
    the 2007 record of the same player."""
    if new[0] == 0 and new[1] == 0:
        return bytes(PLAYER)
    old = bytearray(base or bytes(PLAYER))
    old[:0x5C] = new[:0x5C]
    old[0x60:0x6F] = new[0x60:0x6F]
    for name, bit, count in fields:
        if bit < 0x5C * 8 or 0x60 * 8 <= bit < 0x6F * 8:
            continue
        place = target(name, bit, count)
        if place is None:
            continue
        value = get_bits(new, place[0], place[1])
        if name == "BirthYear" and value:
            value -= 1
        set_bits(old, bit, count, value)
    set_bits(old, *VOICE, voice_2007(get_bits(new, *VOICE)))
    for _, frm, bits, to, to_bits in MOD_2008:
        set_bits(old, frm, bits, get_bits(new, to, to_bits))
    set_bits(old, *FACE, face_2007(get_bits(new, *FACE)))
    set_bits(old, *SKIN, skin_2007(get_bits(new, *SKIN)))
    return bytes(old)


def emit_cpp(repo):
    """The field table runtime/src/mods.cpp uses to convert PowerUp saves when a roster mod loads."""
    lines = ["// Generated by tools/roster/powerup_to_2008.py --emit-cpp from PowerUp's GSPlayer.cs:",
             "// 2007 (bit, width) -> 2008 (bit, width) for every player field outside the byte ranges",
             "// copied as they are (0x00..0x5B, 0x60..0x6E). birthYear marks the year to move by +1.",
             "struct RosterField { uint16_t from, bits, to, toBits; bool birthYear; };",
             "constexpr RosterField kRosterFields[] = {"]
    for name, bit, count in load_fields(repo):
        if bit < 0x5C * 8 or 0x60 * 8 <= bit < 0x6F * 8:
            continue
        place = target(name, bit, count)
        if place is not None:
            year = "true" if name == "BirthYear" else "false"
            lines.append(f"    {{0x{bit:03X}, {count}, 0x{place[0]:03X}, {place[1]}, {year}}},  // {name}")
    lines.append("};")
    lines.append("// 2008 bits PowerUp has no name for, as runs {first, last}; a player found in the original")
    lines.append("// 2008 roster keeps his.")
    lines.append("struct BitRun { uint16_t first, last; };")
    lines.append("constexpr BitRun kRosterUnnamedRuns[] = {")
    runs = []
    for b in sorted(unnamed_2008(load_fields(repo))):
        if runs and runs[-1][1] == b - 1:
            runs[-1][1] = b
        else:
            runs.append([b, b])
    for i in range(0, len(runs), 6):
        lines.append("    " + " ".join(f"{{0x{a:03X}, 0x{b:03X}}}," for a, b in runs[i:i + 6]))
    lines.append("};")
    lines.append("// Abilities only 2008 has, in 2007-layout slots PowerUp+'s 2008 export fills (MOD_2008); only")
    lines.append("// a mod with roster_ids = \"2008\" carries them.")
    lines.append("constexpr RosterField kRosterMod2008Fields[] = {")
    for name, frm, bits, to, to_bits in MOD_2008:
        lines.append(f"    {{0x{frm:03X}, {bits}, 0x{to:03X}, {to_bits}, false}},  // {name}")
    lines.append("};")
    lines.append("// 2007 voice id runs {first, last, offset to 2008's id} (VOICE_RUNS); other ids have no 2008 voice.")
    lines.append("struct VoiceRun { uint16_t first, last; int16_t offset; };")
    lines.append("constexpr VoiceRun kVoiceRuns[] = {")
    for i in range(0, len(VOICE_RUNS), 6):
        lines.append("    " + " ".join(f"{{{a}, {b}, {d}}}," for a, b, d in VOICE_RUNS[i:i + 6]))
    lines.append("};")
    print("\n".join(lines))


def emit_cs(repo):
    """The same table for PowerUp+ (PowerUp/GameSave/PowerPros2008/PowerPros2008Fields.cs)."""
    lines = ["// Generated by tools/roster/powerup_to_2008.py --emit-cs (MLB Power Pros 2008 recomp project)",
             "// from GSPlayer.cs: 2007 (bit, width) -> 2008 (bit, width) for every player field outside the",
             "// byte ranges copied as they are (0x00..0x5B, 0x60..0x6E). BirthYear moves by one year.",
             "namespace PowerUp.GameSave.PowerPros2008",
             "{",
             "  public static class PowerPros2008Fields",
             "  {",
             "    public static readonly (int From, int Bits, int To, int ToBits, bool BirthYear)[] All =",
             "    ["]
    for name, bit, count in load_fields(repo):
        if bit < 0x5C * 8 or 0x60 * 8 <= bit < 0x6F * 8:
            continue
        place = target(name, bit, count)
        if place is not None:
            year = "true" if name == "BirthYear" else "false"
            lines.append(f"      (0x{bit:03X}, {count}, 0x{place[0]:03X}, {place[1]}, {year}), // {name}")
    lines += ["    ];", "",
              "    /// <summary>2007 bit positions PowerUp has no name for (hidden flags and abilities).</summary>",
              "    public static readonly int[] Unmodeled =", "    ["]
    bits = unnamed_2007(load_fields(repo))
    lines += ["      " + ", ".join(f"0x{b:03X}" for b in bits[i:i + 12]) + "," for i in range(0, len(bits), 12)]
    lines += ["    ];", "",
              "    /// <summary>Abilities only 2008 has: 2007-layout slot -> 2008 bits (GSPlayer *2008 fields).</summary>",
              "    public static readonly (int From, int Bits, int To, int ToBits)[] Mod2008 =", "    ["]
    lines += [f"      (0x{frm:03X}, {bits}, 0x{to:03X}, {to_bits}), // {name}" for name, frm, bits, to, to_bits in MOD_2008]
    lines += ["    ];", "",
              "    /// <summary>2007 voice id runs (first, last, offset to 2008's id); other 2007 ids have no 2008 voice.</summary>",
              "    public static readonly (int First, int Last, int Offset)[] VoiceRuns =", "    ["]
    lines += ["      " + " ".join(f"({a}, {b}, {d})," for a, b, d in VOICE_RUNS[i:i + 6]) for i in range(0, len(VOICE_RUNS), 6)]
    lines += ["    ];", "  }", "}"]
    print("\n".join(lines))


def main(argv):
    if argv and argv[0] in ("--emit-cpp", "--emit-cs"):
        repo = Path(__file__).resolve().parents[2] / "local" / "vendor" / "PowerUpPlus"
        (emit_cpp if argv[0] == "--emit-cpp" else emit_cs)(repo)
        return
    # --editor-flags keeps PowerUp+'s "Custom Player" switch (the launcher's "Como en el editor");
    # by default every player enters unedited, like [mods] native_roster = true.
    native = "--editor-flags" not in argv
    argv = [a for a in argv if a != "--editor-flags"]
    save, original, out = Path(argv[0]), Path(argv[1]), Path(argv[2])
    name = argv[3] if len(argv) > 3 else save.stem
    repo = Path(__file__).resolve().parents[2] / "local" / "vendor" / "PowerUpPlus"
    fields = load_fields(repo)
    old_block = save.read_bytes()[SAVE_BLOCK_2007:SAVE_BLOCK_2007 + BLOCK]
    pack = bytearray(original.read_bytes())
    if pack[:8] != b"PACKWII " or len(pack) != PACK_BLOCK_2008 + BLOCK:
        sys.exit("not MLB Power Pros 2008's game_data.pack")
    originals = originals_by_name(pack[PACK_BLOCK_2008:])
    empty = empty_record(pack[PACK_BLOCK_2008:])
    manifest = save.parent.parent / "mod.toml"
    ids_2008 = manifest.is_file() and re.search(r'(?m)^roster_ids\s*=\s*"2008"', manifest.read_text("utf-8")) is not None
    typical = typical_unnamed(pack[PACK_BLOCK_2008:], fields)
    players = b"".join(convert_player(old_block[i * PLAYER:(i + 1) * PLAYER], fields, originals, ids_2008, empty, typical, native)
                       for i in range(SLOTS))
    pack[PACK_BLOCK_2008:PACK_BLOCK_2008 + SLOTS * PLAYER] = players
    rest = SLOTS * PLAYER
    pack[PACK_BLOCK_2008 + rest:PACK_BLOCK_2008 + BLOCK] = convert_rest(old_block[rest:], pack[PACK_BLOCK_2008 + rest:PACK_BLOCK_2008 + BLOCK])
    (out / "project").mkdir(parents=True, exist_ok=True)
    (out / "project" / "game_data.pack").write_bytes(pack)
    (out / "mod.toml").write_text(
        f'name = "{name}"\nauthor = "PowerUp+ (convertido a 2008)"\nversion = "1"\n'
        f'description = "Roster convertido desde {save.name} (MLB Power Pros 2007) al formato de 2008. '
        'Afecta a todos los modos, incluido MLB Life."\n', encoding="utf-8")
    print(f"wrote {out}")


if __name__ == "__main__":
    main(sys.argv[1:])
