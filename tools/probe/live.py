"""Drive the game live through the season probe (WIICOMPILED_PAD_LIVE), for short research runs.
  live.py run [KEY=VAL ...]      start the game (mods from MODSLIST / MODS); returns when the game exits
  live.py do STEP ...            A B X Y Z L R START up down left right cup cdown cleft cright (xN repeats: A*3),
                                 hold:NAME:MS, poke:ADDR:HEX, peek:ADDR:N (prints the bytes), wait:SECONDS,
                                 until:REGEX[:SECONDS] (a game.log line after the latest mark), mark,
                                 untilpeek:ADDR:N:HEX[:SECONDS] (pressing nothing, until those bytes read HEX),
                                 spam:NAME:REGEX[:SECONDS] (press NAME every second until a game.log line matches),
                                 shot:NAME, dump:NAME
  live.py stop
Wait on what the game says (until:, peek:) rather than on fixed times: menus lag under load.
Files in local/tmp/season (cmd.txt, game.log, pid, shots, dumps)."""
import os, re, subprocess, sys, time
DIR = r"D:\MLBPowerPros2008Recomp\local\tmp\season"
BUILD = r"D:\MLBPowerPros2008Recomp\local\wiicompiled-mlb\build"
CMD = os.path.join(DIR, "cmd.txt")
KEYS = dict(A=0x100, B=0x200, X=0x400, Y=0x800, START=0x1000, Z=0x10, R=0x20, L=0x40,
            up=0x8, down=0x4, right=0x2, left=0x1, cright=0x10000, cleft=0x20000, cup=0x40000, cdown=0x80000)
os.makedirs(DIR, exist_ok=True)


def send(text, timeout=20):
    open(CMD + ".tmp", "w").write(text)
    os.replace(CMD + ".tmp", CMD)  # the game must never see a half-written command
    t = time.time()
    while os.path.exists(CMD):
        if time.time() - t > timeout:
            os.remove(CMD)
            sys.exit(f"game did not take '{text}'")
        time.sleep(0.02)


def run(extra):
    cfg = os.path.join(BUILD, "UserData", "Config.toml")
    backup = open(cfg, "rb").read()
    env = dict(os.environ, WIICOMPILED_PAD_LIVE=CMD, WIICOMPILED_SEASON_LOG="1", WIICOMPILED_SKIP_LAUNCHER="1")
    env.pop("WIICOMPILED_MATCH_BENCH", None)
    env["PATH"] = r"D:\MLBPowerPros2008Recomp\local\tools\llvm-mingw-20260616-ucrt-x86_64\bin;" + env["PATH"]
    for kv in extra:
        k, v = kv.split("=", 1)
        env[k] = v
    try:
        mods = os.environ["MODSLIST"].encode() if os.environ.get("MODSLIST") else b"[]" if os.environ.get("MODS") == "none" else b'["SeasonProbe"]' if os.environ.get("MODS") == "probe" else b'["SeasonProbe", "MLBSchedules"]' if os.environ.get("MODS") == "sched" else b'["SeasonProbe", "CalendarioPrueba"]'
        text = re.sub(rb"(?m)^enabled = \[.*\]", b"enabled = " + mods, backup)
        if os.environ.get("NAND"):  # NAND=<dir>: this run's NAND root ([paths] nand_root)
            text = re.sub(rb"(?m)^\[paths\]\s*$", b"[paths]\nnand_root = '" + os.environ["NAND"].replace("\\", "/").encode() + b"'", text, count=1)
        if os.environ.get("SEASONS"):  # SEASONS=20: [season] max_seasons for this run only
            text = re.sub(rb"(?ms)^\[season\].*?(?=^\[|\Z)", b"", text) + b"\n[season]\nmax_seasons = " + os.environ["SEASONS"].encode() + b"\n"
        if os.environ.get("UDH"):  # UDH=1 / UDH=0: [season] universal_dh = true / false for this run only
            value = b"false" if os.environ["UDH"] == "0" else b"true"
            text = re.sub(rb"(?m)^universal_dh = .*\r?\n", b"", text)
            if b"[season]" in text:
                text = re.sub(rb"(?m)^\[season\][ \t]*\r?$", b"[season]\nuniversal_dh = " + value, text, count=1)
            else:
                text += b"\n[season]\nuniversal_dh = " + value + b"\n"
        if os.environ.get("ML"):  # ML=1: [season] modern_leagues = true for this run only
            text = re.sub(rb"(?m)^modern_leagues = .*\r?\n", b"", text)
            text = re.sub(rb"(?m)^\[season\][ \t]*\r?$", b"[season]\nmodern_leagues = true", text, count=1)
        open(cfg, "wb").write(text)
        with open(os.path.join(DIR, "game.log"), "w", encoding="utf-8", errors="replace") as log:
            t0 = time.time()
            p = subprocess.Popen([os.path.join(BUILD, "WiiCompiled.exe")], cwd=BUILD, env=env, stdout=subprocess.PIPE,
                                 stderr=subprocess.STDOUT, text=True, errors="replace")
            open(os.path.join(DIR, "pid"), "w").write(str(p.pid))
            for line in p.stdout:
                log.write(f"{time.time() - t0:7.2f} {line}")
                log.flush()
            p.wait()
    finally:
        open(cfg, "wb").write(backup)


def menu_shown(path):
    """The Season mode menu: its button column matches the reference crop from x2.png."""
    from PIL import Image, ImageChops
    ref = Image.open(os.path.join(DIR, "menu_ref.png")).convert("RGB").crop((20, 200, 470, 600))
    cur = Image.open(path).convert("RGB").resize((1920, 1080)).crop((20, 200, 470, 600))
    diff = ImageChops.difference(ref, cur).convert("L")
    return sum(diff.getdata()) / (450 * 400) < 18


LOG = os.path.join(DIR, "game.log")


def log_size():
    return os.path.getsize(LOG) if os.path.exists(LOG) else 0


def until(pattern, timeout, since):
    """The first game.log line after byte `since` matching pattern; exits when none comes in time."""
    rx, t = re.compile(pattern), time.time()
    while time.time() - t < timeout:
        with open(LOG, "rb") as f:
            f.seek(since)
            for line in f.read().decode("utf-8", "replace").splitlines():
                if rx.search(line):
                    return line
        time.sleep(0.2)
    sys.exit(f"no '{pattern}' in {timeout:.0f} s")


def peek(address, size):
    since = log_size()
    send(f"peek {address} {size}")
    return until(rf"\[season\] peek {int(address, 16):08X} ", 10, since).split()[-1]


def do(steps):
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import winshot
    pid = int(open(os.path.join(DIR, "pid")).read())
    mark = log_size()
    for step in steps:
        if step == "mark":
            mark = log_size()
        elif step.startswith("until:"):
            pattern, _, seconds = step[6:].rpartition(":")
            if not pattern or not seconds.replace(".", "").isdigit():
                pattern, seconds = step[6:], "60"
            print(until(pattern, float(seconds), mark).strip())
            mark = log_size()
        elif step.startswith("spam:"):
            name, _, rest = step[5:].partition(":")
            pattern, _, seconds = rest.rpartition(":")
            if not pattern or not seconds.replace(".", "").isdigit():
                pattern, seconds = rest, "120"
            rx, t, found = re.compile(pattern), time.time(), None
            while not found:
                if time.time() - t > float(seconds):
                    sys.exit(f"no '{pattern}' after pressing {name} for {seconds} s")
                send(f"pad {KEYS[name]:x} 150")
                time.sleep(1)
                with open(LOG, "rb") as f:
                    f.seek(mark)
                    found = next((l for l in f.read().decode("utf-8", "replace").splitlines() if rx.search(l)), None)
            print(found.strip())
            mark = log_size()
        elif step.startswith("untilpeek:"):
            _, addr, size, want, *rest = step.split(":")
            t, timeout = time.time(), float(rest[0]) if rest else 60
            while (got := peek(addr, int(size))) != want.upper():
                if time.time() - t > timeout:
                    sys.exit(f"{addr} reads {got}, not {want} after {timeout:.0f} s")
                time.sleep(0.5)
            print("peek", addr, got)
        elif step.startswith("peek:"):
            _, addr, size = step.split(":")
            print("peek", addr, peek(addr, int(size)))
        elif step.startswith("wait:"):
            time.sleep(float(step[5:]))
        elif step.startswith("shot:"):
            time.sleep(0.3)
            path = os.path.join(DIR, step[5:] + ".png")
            print(path, winshot.shot(pid, path))
            from PIL import Image
            Image.open(path).resize((800, 450)).save(path[:-4] + "_s.png")
        elif step.startswith("tomenu:"):
            # A, one press at a time, until the Season menu shows (at most N presses); stop on anything else
            path = os.path.join(DIR, "tomenu.png")
            for n in range(int(step[7:])):
                time.sleep(1.2)
                winshot.shot(pid, path)
                if menu_shown(path):
                    print("menu after", n, "presses"); break
                send("pad 100 120")
            else:
                print("menu NOT reached")
        elif step.startswith("dump:"):
            send("dump " + step[5:])
            print("dumped", step[5:])
        elif step.startswith("poke:"):
            _, addr, data = step.split(":")
            send(f"poke {addr} {data}")
            print("poked", addr, data)
        elif step.startswith("hold:"):
            _, name, ms = step.split(":")
            send(f"pad {KEYS[name]:x} {ms}")
            time.sleep(int(ms) / 1000 + 0.3)
        else:
            name, _, n = step.partition("*")
            for _ in range(int(n or 1)):
                send(f"pad {KEYS[name]:x} 120")
                time.sleep(0.45)


if __name__ == "__main__":
    if sys.argv[1] == "run":
        run(sys.argv[2:])
    elif sys.argv[1] == "do":
        do(sys.argv[2:])
    elif sys.argv[1] == "stop":
        subprocess.run(["taskkill", "/F", "/PID", open(os.path.join(DIR, "pid")).read().strip()])

