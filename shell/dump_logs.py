"""Save a snapshot of the previous run's last moments to /share/ha_crash_dumps.

Run on every Home Assistant start (see the "HA Start Log Dump" automation), because the
host journal and the Core log rotate away after a few days, which makes a crash hard to
investigate afterwards. Every section is written independently, so one failing source
(for example the Supervisor API) never stops the others.
"""

import datetime
import os
import pathlib
import re
import shutil
import sqlite3
import urllib.request

OUT_DIR = pathlib.Path("/share/ha_crash_dumps")
KEEP = 20
SUPERVISOR = "http://supervisor"
DB_PATH = "/config/home-assistant_v2.db"
FAULT_FILE = "/config/home-assistant.log.fault"

ANSI = re.compile(r"\x1b\[[0-9;]*m")
PROBLEM = re.compile(
    r" (WARNING|ERROR|CRITICAL) |Traceback|signal \d+|exit code|Fatal|segfault",
)
DIR_NAME = re.compile(r"^\d{4}-\d{2}-\d{2}_\d{6}_UTC$")


def supervisor_log(path: str, lines: int) -> list[str]:
    """Return the last lines of a Supervisor log endpoint."""
    request = urllib.request.Request(
        SUPERVISOR + path,
        headers={
            "Authorization": f"Bearer {os.environ['SUPERVISOR_TOKEN']}",
            "Range": f"entries=:-{lines}:",
        },
    )
    with urllib.request.urlopen(request, timeout=90) as response:
        text = response.read().decode(errors="replace")
    return ANSI.sub("", text).splitlines()


def write(folder: pathlib.Path, name: str, lines: list[str]) -> None:
    """Write lines to a file in the snapshot folder."""
    (folder / name).write_text("\n".join(lines) + "\n")


def main() -> None:
    """Create the snapshot and prune old ones."""
    now = datetime.datetime.now(datetime.timezone.utc)
    folder = OUT_DIR / now.strftime("%Y-%m-%d_%H%M%S_UTC")
    folder.mkdir(parents=True, exist_ok=True)
    errors: list[str] = []

    try:
        core = supervisor_log("/core/logs", 20000)
        write(folder, "core_last_3000.log", core[-3000:])
        write(folder, "core_warnings_errors.log", [x for x in core if PROBLEM.search(x)])
    except Exception as err:  # noqa: BLE001
        errors.append(f"core log: {err!r}")

    try:
        write(folder, "host_last_2000.log", supervisor_log("/host/logs", 2000))
    except Exception as err:  # noqa: BLE001
        errors.append(f"host log: {err!r}")

    try:
        text = pathlib.Path(FAULT_FILE).read_text(errors="replace")
        blocks = text.split("Fatal Python error")
        last = ["Fatal Python error" + block for block in blocks[-2:] if len(blocks) > 1]
        write(folder, "fault_last_2.txt", "".join(last).splitlines()[:600])
    except Exception as err:  # noqa: BLE001
        errors.append(f"fault file: {err!r}")

    try:
        database = sqlite3.connect(f"file:{DB_PATH}?mode=ro&immutable=1", uri=True)
        rows = database.execute(
            "select run_id, start, end, closed_incorrect from recorder_runs "
            "order by run_id desc limit 5"
        ).fetchall()
        write(
            folder,
            "recorder_runs.txt",
            ["run_id | start (UTC) | end (UTC) | closed_incorrect (1 = unclean)"]
            + [" | ".join(str(value) for value in row) for row in rows],
        )
    except Exception as err:  # noqa: BLE001
        errors.append(f"recorder runs: {err!r}")

    try:
        keys = ("MemTotal", "MemAvailable", "SwapTotal", "SwapFree", "Zswap", "Zswapped")
        memory = [
            line.strip()
            for line in pathlib.Path("/proc/meminfo").read_text().splitlines()
            if line.split(":")[0] in keys
        ]
        pressure = pathlib.Path("/proc/pressure/memory").read_text().splitlines()
        write(folder, "memory.txt", memory + pressure)
    except Exception as err:  # noqa: BLE001
        errors.append(f"memory: {err!r}")

    write(
        folder,
        "summary.txt",
        [f"Snapshot taken {now.isoformat()} (UTC), at Home Assistant start"]
        + [f"Not saved: {error}" for error in errors],
    )

    old = sorted(p for p in OUT_DIR.iterdir() if p.is_dir() and DIR_NAME.match(p.name))
    for path in old[:-KEEP]:
        shutil.rmtree(path)


main()
