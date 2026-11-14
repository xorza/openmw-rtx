#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <vulkan/vulkan_core.h>

#include <components/rtx/common/error.hpp>
#include <components/rtx/renderer/memoryreport.hpp>

namespace Rtx
{
    class Device;

    /// Name of a `VkResult` as it is spelled in the header, for messages.
    std::string_view resultName(VkResult result);

    /// Throws `DeviceError` naming `call` and the result unless `result` is `VK_SUCCESS`, for a
    /// call on the device. `VK_ERROR_DEVICE_LOST` is `deviceFailed` with what the device says about
    /// the fault, which is the one moment that question may be asked. `VK_INCOMPLETE` is a failure
    /// here; `enumerateVk` is the one caller that answers it. One overload wherever a device
    /// stands, so no call that can lose it takes the path that cannot say so (`RtxSourceTreeTest`).
    void checkVk(const Device& device, VkResult result, const char* call);

    /// The same for a call on the instance or a physical device, which no device stands behind to
    /// lose: before one is made, or about the card rather than it.
    void checkVkWithoutDevice(VkResult result, const char* call);

    /// **Ends the process as a crash where the device was lost or stopped answering**, which
    /// nothing below the seam can go on from: the report is taken where the failure was found,
    /// before anything unwinds, so its stacks show the call that found it and its notes what every
    /// thread was doing. `message` goes to the log whole, which a report's reason is too short for.
    [[noreturn]] void deviceFailed(const std::string& message);

    /// The same as the first, for a bring-up call whose failure means this machine cannot run the
    /// backend: throws `Unsupported`, which is a machine to skip rather than a fault to report.
    void checkVkSupport(VkResult result, const char* call);

    /// How long a wait on the device may take before it is called a failure — a canary and not a
    /// budget: the longest honest submit measured is a scene rebuild at a fifth of a second.
    inline constexpr std::uint64_t sPatience = 10'000'000'000ull;

    /// `checkVk` for a wait that was given `patience`: `VK_TIMEOUT` is `deviceFailed` under a
    /// message of its own, because it is the one that says nothing about what the device thought
    /// was wrong — only that it stopped saying anything at all. The buffer markers say where the
    /// queue stands (`FaultReport::describeMarkers`).
    void checkVkWait(const Device& device, VkResult result, const char* what, std::uint64_t patience);

    /// Waits for `fence`, and ends the process through `deviceFailed` naming `what` if the device
    /// does not answer in time: with `UINT64_MAX` a stalled submit took the whole test suite with it.
    ///
    /// @param patience nanoseconds to allow; a parameter so a test can reach the failure.
    void awaitVk(const Device& device, VkFence fence, const char* what, std::uint64_t patience = sPatience);

    /// **What was in flight when the device was lost**, as lines for its report: the submits after
    /// the last the host knew finished, up to the last made, and every heap as `describeMemory`
    /// reads them.
    std::string describeInFlight(std::uint64_t lastSubmitted, std::uint64_t knownFinished, const MemoryReport& memory);

    /// What a wait that ran out is called.
    std::string timedOut(const char* what, std::uint64_t patience);

    /// What an enumeration whose list never stopped growing is called.
    std::string neverSettled(const char* call);

    /// How many times an enumeration may be told the driver's list is longer than it just said,
    /// bounded for the reason `sPatience` is.
    inline constexpr int sEnumerationTries = 4;

    /// The two-call enumeration every Vulkan list query is made of, asked until the driver has
    /// nothing left to add: `VK_INCOMPLETE` is a legal answer to the second call. `check` raises
    /// what a call answered.
    ///
    /// @param call the entry point's own name, for the message a failure carries.
    /// @param enumerate invoked as `(std::uint32_t* count, T* into)`, exactly as the entry point
    ///        takes them.
    /// @param prototype what every element is set to before the fill, which is how a structure that
    ///        has to carry its own `sType` is given one.
    template <class T, class Check, class Enumerate>
    std::vector<T> enumerateChecked(const Check& check, const char* call, Enumerate&& enumerate, const T& prototype)
    {
        std::vector<T> into;

        for (int asked = 0; asked < sEnumerationTries; ++asked)
        {
            std::uint32_t count = 0;
            check(enumerate(&count, nullptr), call);
            if (count == 0)
                return into;

            into.assign(count, prototype);

            const VkResult filled = enumerate(&count, into.data());
            if (filled == VK_INCOMPLETE)
                continue;

            check(filled, call);

            // Down to what the fill wrote and never up. A list that shrank between the two
            // calls leaves elements nothing touched, and a driver that reported more than it was
            // given room for would otherwise be handed back elements that are not there.
            into.resize(std::min(static_cast<std::size_t>(count), into.size()));
            return into;
        }

        throw DeviceError(neverSettled(call));
    }

    /// A list of the instance or a physical device, as `enumerateChecked` asks it.
    template <class T, class Enumerate>
    std::vector<T> enumerateVk(const char* call, Enumerate&& enumerate, const T& prototype = T{})
    {
        return enumerateChecked(checkVkWithoutDevice, call, std::forward<Enumerate>(enumerate), prototype);
    }

    /// A list of `device`'s, as `enumerateChecked` asks it.
    template <class T, class Enumerate>
    std::vector<T> enumerateVk(const Device& device, const char* call, Enumerate&& enumerate, const T& prototype = T{})
    {
        return enumerateChecked([&](const VkResult result, const char* const named) { checkVk(device, result, named); },
            call, std::forward<Enumerate>(enumerate), prototype);
    }

    /// Logs `failure` and what was raised. `tearDown` calls this and nothing else should.
    void reportTornDown(std::string_view failure, const char* raised);

    /// Runs `work` and reports whatever it raises rather than letting it out, which every teardown
    /// goes through because a destructor is `noexcept`. A lost device ends the process anyway,
    /// through `deviceFailed`. Wherever a caller can act on a failure, `checkVk` is the right call.
    ///
    /// @param failure the whole clause the log states, which the raised message is appended to.
    template <class Work>
    void tearDown(std::string_view failure, Work&& work)
    {
        try
        {
            std::forward<Work>(work)();
        }
        catch (const std::exception& raised)
        {
            reportTornDown(failure, raised.what());
        }
        catch (...)
        {
            // The promise is that nothing leaves, so it may not depend on what was thrown.
            // Nothing in this tree raises anything else, and a promise with a hole in it is not one.
            reportTornDown(failure, "something that is not an exception");
        }
    }
}
