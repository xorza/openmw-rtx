"""What `CMakePresets.json` says, read the way CMake reads it, for the two questions the driver asks
of it: whether a build directory was configured from what the presets say now, and what environment
a flavour's programs run under."""

import hashlib
import json
import re
from pathlib import Path

from omw.system import ROOT, Refusal

_MACRO = re.compile(r"\$(p?env)\{([A-Za-z_][A-Za-z0-9_]*)\}")

# **What CMake reads from the environment on its own**, outside any preset: the compilers, and where
# the SDKs are. A change to one is a change to what a configure would find.
IMPLICIT_INPUTS = ("CC", "CXX", "VULKAN_SDK", "NGX_ROOT", "CMAKE_PREFIX_PATH")


def preset_files(root: Path = ROOT) -> list[Path]:
    files = [root / "CMakePresets.json"]
    user = root / "CMakeUserPresets.json"
    if user.is_file():
        files.append(user)
    return files


def digest(env: dict[str, str], root: Path = ROOT) -> str:
    """**Everything a preset is expanded from**: the preset files, the value of every environment
    variable they name, and what CMake reads from the environment besides. Two directories
    configured from equal digests were handed the same cache variables."""
    hashed = hashlib.sha256()
    names = set(IMPLICIT_INPUTS)
    for file in preset_files(root):
        text = file.read_text()
        if "include" in json.loads(text):
            raise Refusal(f"{file.name} includes another presets file, which the digest does not read")
        hashed.update(text.encode())
        names.update(name for _, name in _MACRO.findall(text))
    for name in sorted(names):
        hashed.update(f"{name}={env.get(name, '')}\n".encode())
    return hashed.hexdigest()


def _expand(value: str, env: dict[str, str]) -> str:
    return _MACRO.sub(lambda match: env.get(match.group(2), ""), value)


def test_environment(name: str, env: dict[str, str], root: Path = ROOT) -> dict[str, str]:
    """The `environment` of the test preset `name`, with what it inherits, expanded against `env`:
    what CTest runs the tests under, and so what every program of that flavour runs under. Only
    `$env{}` and `$penv{}` are read, which is all the presets use; a variable a preset names as
    `null` is taken away."""
    presets: dict[str, dict] = {}
    for file in preset_files(root):
        for preset in json.loads(file.read_text()).get("testPresets", []):
            presets[preset["name"]] = preset

    def gathered(preset_name: str) -> dict[str, str | None]:
        preset = presets.get(preset_name)
        if preset is None:
            raise Refusal(f"CMakePresets.json has no test preset called {preset_name}")
        merged: dict[str, str | None] = {}
        # The earlier parent wins a variable both name, and the preset itself wins over them all.
        for parent in reversed(preset.get("inherits", [])):
            merged.update(gathered(parent))
        merged.update(preset.get("environment", {}))
        return merged

    result = dict(env)
    for variable, value in gathered(name).items():
        if value is None:
            result.pop(variable, None)
        else:
            result[variable] = _expand(value, env)
    return result


def has_test_preset(name: str, root: Path = ROOT) -> bool:
    return any(preset["name"] == name for file in preset_files(root)
               for preset in json.loads(file.read_text()).get("testPresets", []))
