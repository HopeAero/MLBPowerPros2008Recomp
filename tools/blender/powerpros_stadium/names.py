# SPDX-License-Identifier: GPL-3.0-or-later
"""Stadium codes of MLB Power Pros 2008 (stadium_<code>.pack) and readable names."""
from .i18n import T

TEAMS = [
    ("ari", "Arizona Diamondbacks — Chase Field"),
    ("atl", "Atlanta Braves — Turner Field"),
    ("bal", "Baltimore Orioles — Camden Yards"),
    ("bos", "Boston Red Sox — Fenway Park"),
    ("chc", "Chicago Cubs — Wrigley Field"),
    ("cws", "Chicago White Sox — U.S. Cellular Field"),
    ("cin", "Cincinnati Reds — Great American Ball Park"),
    ("cle", "Cleveland Indians — Jacobs Field"),
    ("col", "Colorado Rockies — Coors Field"),
    ("det", "Detroit Tigers — Comerica Park"),
    ("fla", "Florida Marlins — Dolphin Stadium"),
    ("hou", "Houston Astros — Minute Maid Park"),
    ("kc", "Kansas City Royals — Kauffman Stadium"),
    ("laa", "Los Angeles Angels — Angel Stadium"),
    ("lad", "Los Angeles Dodgers — Dodger Stadium"),
    ("mil", "Milwaukee Brewers — Miller Park"),
    ("min", "Minnesota Twins — Metrodome"),
    ("nym", "New York Mets — Shea Stadium"),
    ("nyy", "New York Yankees — Yankee Stadium"),
    ("oka", "Oakland Athletics — McAfee Coliseum"),
    ("phi", "Philadelphia Phillies — Citizens Bank Park"),
    ("pit", "Pittsburgh Pirates — PNC Park"),
    ("sd", "San Diego Padres — Petco Park"),
    ("sf", "San Francisco Giants — AT&T Park"),
    ("sea", "Seattle Mariners — Safeco Field"),
    ("st", "St. Louis Cardinals — Busch Stadium"),
    ("tb", "Tampa Bay Devil Rays — Tropicana Field"),
    ("tex", "Texas Rangers — Rangers Ballpark"),
    ("tor", "Toronto Blue Jays — Rogers Centre"),
    ("wsh", "Washington Nationals"),
]


def stadiums():
    """(code, readable name) of every stadium, in the add-on's language."""
    return (TEAMS
            + [(f"org{n:02}", T(f"Estadio original {n}", f"Original stadium {n}")) for n in range(1, 7)]
            + [("ml01", T("ml01 (por identificar)", "ml01 (unidentified)")),
               ("bullpen", "Bullpen"),
               ("bullpen2", "Bullpen 2"),
               ("season", T("season (por identificar)", "season (unidentified)"))])


CODES = [c for c, _ in stadiums()]


def name(code):
    return dict(stadiums()).get(code, code)


def valid_code(code):
    """A new stadium's code: 2 to 16 lowercase letters, digits or _."""
    return 2 <= len(code) <= 16 and all(c.isascii() and (c.isalnum() or c == "_") for c in code) and code.islower()
