#include <cstring>
#include <vector>

#include <AL/alc.h>
#include <AL/alext.h>

#include "openalutil.hpp"

std::vector<std::string> Launcher::enumerateOpenALDevices()
{
    std::vector<std::string> devlist;
    const ALCchar* devnames = nullptr;

    if (alcIsExtensionPresent(nullptr, "ALC_ENUMERATE_ALL_EXT"))
    {
        devnames = alcGetString(nullptr, ALC_ALL_DEVICES_SPECIFIER);
    }
    else
    {
        devnames = alcGetString(nullptr, ALC_DEVICE_SPECIFIER);
    }

    while (devnames && *devnames)
    {
        devlist.emplace_back(devnames);
        devnames += strlen(devnames) + 1;
    }
    return devlist;
}

std::vector<std::string> Launcher::enumerateOpenALDevicesHrtf()
{
    std::vector<std::string> ret;

    ALCdevice* device = alcOpenDevice(nullptr);
    if (device)
    {
        if (alcIsExtensionPresent(device, "ALC_SOFT_HRTF"))
        {
            LPALCGETSTRINGISOFT alcGetStringiSOFT = nullptr;
            void* funcPtr = alcGetProcAddress(device, "alcGetStringiSOFT");
            // An object pointer becomes a function pointer, which no cast spells portably.
            // NOLINTNEXTLINE(bugprone-bitwise-pointer-cast)
            memcpy(static_cast<void*>(&alcGetStringiSOFT), static_cast<const void*>(&funcPtr), sizeof(funcPtr));
            ALCint numHrtf = 0;
            alcGetIntegerv(device, ALC_NUM_HRTF_SPECIFIERS_SOFT, 1, &numHrtf);
            ret.reserve(static_cast<std::size_t>(numHrtf));
            for (ALCint i = 0; i < numHrtf; ++i)
            {
                const ALCchar* entry = alcGetStringiSOFT(device, ALC_HRTF_SPECIFIER_SOFT, i);
                if (strcmp(entry, "") == 0)
                    break;
                ret.emplace_back(entry);
            }
        }
        alcCloseDevice(device);
    }
    return ret;
}
