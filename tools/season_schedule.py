# SPDX-License-Identifier: GPL-3.0-or-later
"""Make a Season mode schedule (a mod's schedule/<year>.csv) from MLB's public schedule.

python tools/season_schedule.py mlb YEAR OUT.csv [--json FILE]   regular season of YEAR, from
    statsapi.mlb.com (or a saved response of /api/v1/schedule?sportId=1&gameType=R...)
python tools/season_schedule.py selftest

The CSV is date,home,away,time with team codes and Eastern start times (format in
runtime/include/season_schedule.h). Postponed games stay on their original date and their make-up
games are left out, as in the schedule published before a season. The game's table has 16 games a
day from March 1 to October 31; the runtime drops what does not fit and says so in its log.
"""
import datetime
import json
import sys
import urllib.request

# MLB team ids -> the codes season_schedule.h knows.
TEAMS = {108: "LAA", 109: "ARI", 110: "BAL", 111: "BOS", 112: "CHC", 113: "CIN", 114: "CLE", 115: "COL",
         116: "DET", 117: "HOU", 118: "KC", 119: "LAD", 120: "WSH", 121: "NYM", 133: "OAK", 134: "PIT",
         135: "SD", 136: "SEA", 137: "SF", 138: "STL", 139: "TB", 140: "TEX", 141: "TOR", 142: "MIN",
         143: "PHI", 144: "ATL", 145: "CWS", 146: "MIA", 147: "NYY", 158: "MIL"}


def eastern(utc: datetime.datetime) -> datetime.datetime:
    """UTC to US Eastern time (DST from the 2nd Sunday of March to the 1st Sunday of November, 2:00)."""
    def sunday(year: int, month: int, nth: int) -> datetime.date:
        first = datetime.date(year, month, 1)
        return first + datetime.timedelta(days=(6 - first.weekday()) % 7 + 7 * (nth - 1))
    start = datetime.datetime.combine(sunday(utc.year, 3, 2), datetime.time(7))   # 2:00 EST in UTC
    end = datetime.datetime.combine(sunday(utc.year, 11, 1), datetime.time(6))    # 2:00 EDT in UTC
    return utc - datetime.timedelta(hours=4 if start <= utc < end else 5)


def rows(schedule: dict) -> list[tuple[str, str, str, str]]:
    out = []
    for day in schedule.get("dates", []):
        for game in day["games"]:
            if game.get("gameType") != "R" or game.get("rescheduledFrom") or game.get("resumedFrom"):
                continue  # not regular season, a postponed game's make-up or a suspended game's resumption
            home, away = game["teams"]["home"]["team"]["id"], game["teams"]["away"]["team"]["id"]
            if home not in TEAMS or away not in TEAMS:
                continue
            start = eastern(datetime.datetime.strptime(game["gameDate"], "%Y-%m-%dT%H:%M:%SZ"))
            time = "" if game["status"].get("startTimeTBD") else start.strftime("%H:%M")
            # The day it was scheduled (a postponed game's officialDate is its make-up's); a late West
            # Coast game is already the next day in UTC, so not gameDate's date either.
            out.append((day["date"], TEAMS[home], TEAMS[away], time))
    return sorted(set(out))


def fetch(year: int) -> dict:
    url = (f"https://statsapi.mlb.com/api/v1/schedule?sportId=1&gameType=R"
           f"&startDate={year}-03-01&endDate={year}-10-31")
    with urllib.request.urlopen(url, timeout=60) as response:
        return json.load(response)


def selftest() -> None:
    assert eastern(datetime.datetime(2026, 7, 1, 23, 5)) == datetime.datetime(2026, 7, 1, 19, 5)   # EDT
    assert eastern(datetime.datetime(2026, 3, 1, 18, 5)) == datetime.datetime(2026, 3, 1, 13, 5)   # EST
    assert eastern(datetime.datetime(2026, 3, 8, 7, 0)).hour == 3                                  # DST begins
    game = {"gameType": "R", "gameDate": "2026-04-02T02:10:00Z", "officialDate": "2026-04-01",
            "teams": {"home": {"team": {"id": 119}}, "away": {"team": {"id": 109}}}}
    game["status"] = {"detailedState": "Final"}
    makeup = dict(game, rescheduledFrom="2026-03-30")
    assert rows({"dates": [{"date": "2026-04-01", "games": [game, makeup]}]}) == [("2026-04-01", "LAD", "ARI", "22:10")]
    print("season_schedule selftest: ok")


def main() -> None:
    args = sys.argv[1:]
    if args[:1] == ["selftest"]:
        return selftest()
    if len(args) < 3 or args[0] != "mlb":
        sys.exit(__doc__)
    year, out = int(args[1]), args[2]
    schedule = json.load(open(args[args.index("--json") + 1], encoding="utf-8")) if "--json" in args else fetch(year)
    games = rows(schedule)
    with open(out, "w", encoding="utf-8", newline="\n") as f:
        f.write(f"# MLB {year} regular season, from statsapi.mlb.com\ndate,home,away,time\n")
        f.writelines(",".join(row) + "\n" for row in games)
    per_team = {}
    for _, home, away, _ in games:
        per_team[home] = per_team.get(home, 0) + 1
        per_team[away] = per_team.get(away, 0) + 1
    print(f"{out}: {len(games)} games, {min(per_team.values(), default=0)}-{max(per_team.values(), default=0)} per team")


if __name__ == "__main__":
    main()
