"""MLB Life two-way player's turns to start, from a memory dump (live.py do dump:NAME): his team's games on
the season's schedule ([0x8104B5A0] + 10192, 245 days from day 60, 16 slots of u32 home, u32 away) and every
5th of them, as two_way.cpp's LifeStarter counts them. Checks each turn is a game day, 5+ days apart.
  life_turns.py NAME      reads local/tmp/season/NAME.mem1 and NAME.mem2"""
import datetime, os, sys

DIR = r"D:\MLBPowerPros2008Recomp\local\tmp\season"
mem1 = open(os.path.join(DIR, sys.argv[1] + ".mem1"), "rb").read()
mem2 = open(os.path.join(DIR, sys.argv[1] + ".mem2"), "rb").read()


def r32(a):
    data, o = (mem2, a - 0x90000000) if a >= 0x90000000 else (mem1, a - 0x80000000)
    return int.from_bytes(data[o:o + 4], "big")


x, team = r32(0x8104B5A0), r32(r32(0x8104B59C))
games = [d for d in range(60, 60 + 245)
         if any(team in (r32(x + 10192 + (d - 60) * 256 + s * 16), r32(x + 10192 + (d - 60) * 256 + s * 16 + 4))
                for s in range(16))]
starts = games[::5]
day = lambda d: (datetime.date(2024, 1, 1) + datetime.timedelta(d)).strftime("%m/%d")  # the game's year has 366 days
print(f"team {team}: {len(games)} games; starts {' '.join(day(d) for d in starts)}")
assert games and all(b - a >= 5 for a, b in zip(starts, starts[1:])), "turns closer than 5 days"
print("ok")
