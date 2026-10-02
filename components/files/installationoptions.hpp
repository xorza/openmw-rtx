#pragma once

#include <boost/program_options/options_description.hpp>

namespace Files
{
    /// The options that say which installation a binary runs: its data folders, archives, content
    /// and groundcover files, the content's encoding and the fallback values. One statement for the
    /// game and for every binary that runs the engine, so an `openmw.cfg` reads the same in each;
    /// `OpenMW::readInstallation` reads them.
    void addInstallationOptions(boost::program_options::options_description& description);
}
