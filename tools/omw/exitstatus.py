"""How a run of the harness ends: `RtxTool::ExitStatus` (`apps/rtxtool/model/exitstatus.hpp`), which a test holds
this to. A driver tells "the run broke" from "the run found a difference" by this and not by the report's words."""

from enum import IntEnum


class ExitStatus(IntEnum):
    PASSED = 0
    FAILED = 1
    REFUSED = 2
    DIFFERED = 3
