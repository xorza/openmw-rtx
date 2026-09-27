"""`omw [flavour] game [args]` and `omw [flavour] setup <morrowind dir>`: the game on the newest
quicksave, and the openmw.cfg a fresh box has none of."""

import subprocess
from pathlib import Path

from omw.build import Build
from omw.system import Refusal, run, user_config_dir, user_data_dir


def game(build: Build, args: list[str]) -> int:
    """The newest quicksave of any character unless the line names a save, so the driver names
    nobody's."""
    build.build(["openmw"])
    save: list[str | Path] = []
    if not any(arg.startswith("--load-savegame") for arg in args):
        saves = list((user_data_dir() / "saves").rglob("Quicksave.omwsave"))
        if not saves:
            raise Refusal(f"no Quicksave.omwsave under {user_data_dir() / 'saves'}; name one with --load-savegame")
        save = ["--load-savegame", max(saves, key=lambda found: found.stat().st_mtime)]
    return build.run_here([build.binary("openmw"), "--skip-menu", *save, *args]).returncode


def setup(build: Build, args: list[str]) -> int:
    """**What upstream's wizard does, on a box with no Qt to run it.** A fresh box has no
    `openmw.cfg`, and the harness stages no world until one names the game: `data=` says where
    `Data Files` is, and upstream's `openmw-iniimporter`, built here for the purpose, reads
    `Morrowind.ini` for the content files, the archives and the five hundred `fallback=` values the
    weather, the sky and the lighting are read from — without which the sky is black. The file is
    written where the game reads it, and never over one that is there: a box that has one has
    settings in it."""
    if len(args) != 1:
        raise Refusal("setup takes the Morrowind directory: the one holding Morrowind.ini and Data Files")
    install = Path(args[0]).resolve()
    if not (install / "Morrowind.ini").is_file() or not (install / "Data Files").is_dir():
        raise Refusal("setup takes the Morrowind directory: the one holding Morrowind.ini and Data Files")

    cfg = user_config_dir() / "openmw.cfg"
    if cfg.exists():
        raise Refusal(f"{cfg} is already there; move it away to start over")

    build.configure()
    run(["cmake", "-DBUILD_MWINIIMPORTER=ON", build.dir], env=build.env, stdout=subprocess.DEVNULL)
    build.build(["openmw-iniimporter"])

    cfg.parent.mkdir(parents=True, exist_ok=True)
    cfg.write_text(f'data="{install / "Data Files"}"\n')
    build.run_here([build.binary("openmw-iniimporter"), "--game-files", "--encoding", "win1252",
                    install / "Morrowind.ini", cfg], check=True)
    print(f"wrote {cfg}")
    return 0
