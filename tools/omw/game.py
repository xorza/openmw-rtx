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
    """**The game's own answer and not a copy of it**: `Files` says where the game reads its configuration."""
    printed = build.harness("info", "--folders", check=True, capture_output=True, encoding="utf-8").stdout
    return parse_folders(printed)


def game(build: Build, args: list[str]) -> int:
    """The newest quicksave of any character, unless the line names a save."""
    named = any(arg.startswith("--load-savegame") for arg in args)
    # In one Ninja run: the harness only where it is asked where the saves are.
    build.build(["openmw"] if named else ["openmw", "openmw-rtxtool"])
    save: list[str | Path] = []
    if not named:
        saved = game_folders(build).data / "saves"
        saves = list(saved.rglob("Quicksave.omwsave"))
        if not saves:
            raise Refusal(f"no Quicksave.omwsave under {saved}; name one with --load-savegame")
        save = ["--load-savegame", max(saves, key=lambda found: found.stat().st_mtime)]
    return status(build.run_here([build.binary("openmw"), "--skip-menu", *save, *args]).returncode)


def setup(build: Build, args: list[str]) -> int:
    """**What upstream's wizard does, on a box with no Qt to run it**: `data=` names `Data Files`, and
    `openmw-iniimporter` reads `Morrowind.ini` for the content files, the archives and the `fallback=` values the
    sky and the lighting are read from. Written where the game reads it, and never over one that is there."""
    install = Path(args[0]).resolve() if len(args) == 1 else None
    if install is None or not (install / "Morrowind.ini").is_file() or not (install / "Data Files").is_dir():
        raise Refusal("setup takes the Morrowind directory: the one holding Morrowind.ini and Data Files")

    # In one Ninja run: the harness says where the configuration goes, and the importer writes it.
    build.build(["openmw-rtxtool", "openmw-iniimporter"])
    cfg = game_folders(build).config / "openmw.cfg"
    if cfg.exists():
        raise Refusal(f"{cfg} is already there; move it away to start over")

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
