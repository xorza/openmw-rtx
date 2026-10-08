"""`omw [flavour] game [args]` and `omw [flavour] setup <morrowind dir>`: the game on the newest
quicksave, and the openmw.cfg a fresh box has none of."""

from dataclasses import dataclass
from pathlib import Path

from omw.build import Build
from omw.fetch import partial_of
from omw.system import Refusal, status


@dataclass(frozen=True)
class GameFolders:
    """Where the game reads its openmw.cfg, and where it keeps its saves."""

    config: Path
    data: Path


def parse_folders(printed: str) -> GameFolders:
    """`openmw-rtxtool info --folders`'s two lines, among whatever else the run printed."""
    named = dict(line.split(" ", 1) for line in printed.splitlines() if line.startswith(("config ", "data ")))
    if set(named) != {"config", "data"}:
        raise Refusal("openmw-rtxtool info --folders named no config and data folders")
    return GameFolders(config=Path(named["config"]), data=Path(named["data"]))


def game_folders(build: Build) -> GameFolders:
    """**The game's own answer and not a copy of it**: `Files` answers where the game reads its
    configuration on every system, and a restatement here had already gone wrong on macOS."""
    printed = build.harness("info", "--folders", check=True, capture_output=True, encoding="utf-8").stdout
    return parse_folders(printed)


def game(build: Build, args: list[str]) -> int:
    """The newest quicksave of any character unless the line names a save, so the driver names
    nobody's."""
    build.build(["openmw"])
    save: list[str | Path] = []
    if not any(arg.startswith("--load-savegame") for arg in args):
        saved = game_folders(build).data / "saves"
        saves = list(saved.rglob("Quicksave.omwsave"))
        if not saves:
            raise Refusal(f"no Quicksave.omwsave under {saved}; name one with --load-savegame")
        save = ["--load-savegame", max(saves, key=lambda found: found.stat().st_mtime)]
    return status(build.run_here([build.binary("openmw"), "--skip-menu", *save, *args]).returncode)


def setup(build: Build, args: list[str]) -> int:
    """**What upstream's wizard does, on a box with no Qt to run it.** A fresh box has no
    `openmw.cfg`, and the harness stages no world until one names the game: `data=` says where
    `Data Files` is, and upstream's `openmw-iniimporter`, which every preset builds for this, reads
    `Morrowind.ini` for the content files, the archives and the five hundred `fallback=` values the
    weather, the sky and the lighting are read from — without which the sky is black. The file is
    written where the game reads it, and never over one that is there: a box that has one has
    settings in it."""
    install = Path(args[0]).resolve() if len(args) == 1 else None
    if install is None or not (install / "Morrowind.ini").is_file() or not (install / "Data Files").is_dir():
        raise Refusal("setup takes the Morrowind directory: the one holding Morrowind.ini and Data Files")

    cfg = game_folders(build).config / "openmw.cfg"
    if cfg.exists():
        raise Refusal(f"{cfg} is already there; move it away to start over")

    build.build(["openmw-iniimporter"])
    cfg.parent.mkdir(parents=True, exist_ok=True)
    # Beside its name until the import is whole, or a failed one leaves a file the next setup refuses.
    partial = partial_of(cfg)
    try:
        partial.write_text(f'data="{install / "Data Files"}"\n')
        build.run_here([build.binary("openmw-iniimporter"), "--game-files", "--encoding", "win1252",
                        install / "Morrowind.ini", partial], check=True)
        partial.rename(cfg)
    finally:
        partial.unlink(missing_ok=True)
    print(f"wrote {cfg}")
    return 0
