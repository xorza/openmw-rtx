"""The record a harness run writes (`bench --json`, a tour's `tour.jsonl`; `RtxTool::RunRecord::save`): JSON lines,
`{"header": …}` first, then `{"place": …}` a line as each place was measured, and from a tour `{"findings": …}`
after every leg, the last of which stands."""

import json
from dataclasses import dataclass


@dataclass(frozen=True)
class Record:
    header: dict
    places: tuple[dict, ...]
    findings: dict | None


def read_record(text: str) -> Record:
    """The record `text` holds. **A last line cut short is left out**: a run that died while it wrote has said how far
    it came by every line before it; one cut short anywhere else is no record, and raises as `json` does."""
    lines = [line for line in text.splitlines() if line.strip()]
    header: dict = {}
    places: list[dict] = []
    findings: dict | None = None
    for at, line in enumerate(lines):
        try:
            entry = json.loads(line)
        except json.JSONDecodeError:
            if at + 1 == len(lines):
                break
            raise
        if "header" in entry:
            header = entry["header"]
        elif "place" in entry:
            places.append(entry["place"])
        elif "findings" in entry:
            findings = entry["findings"]
    return Record(header, tuple(places), findings)
