#pragma once

#include <apps/rtxtool/model/tour.hpp>

namespace RtxTool
{
    /// What the content the running game loaded holds, read from its store once, as a tour plans its route where the
    /// world first stands: every exterior cell with land and the highest its ground stands, and every interior with
    /// the content file that first names it, the references it holds and where `coc` puts a player in it. The land's
    /// heights and the rooms' references are read through readers of its own, which keep each file open across its
    /// records, so the game's records are not loaded into.
    WorldMap readWorldMap();
}
