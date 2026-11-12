#pragma once

#include <memory>
#include <string>

#include <components/misc/result.hpp>
#include <components/rtx/renderer/renderer.hpp>

namespace Rtx
{
    class Instance;
}

namespace Rtx::Testing
{
    /// An instance with `options`, or why this machine cannot build one.
    ///
    /// The two ways a machine legitimately has nothing to trace with: no loader, or a loader with no
    /// driver behind it — which fails at `vkCreateInstance` with `VK_ERROR_INCOMPATIBLE_DRIVER`
    /// rather than by handing back an empty device list.
    ///
    /// Shared by the harness, which fails its binary on one, and the tests that build an instance of
    /// their own, which skip on one, so the two cannot come to disagree about which failure is
    /// honest and which is a finding. Whether a device that *does* exist qualifies is a different
    /// question, and `PhysicalDevice::select` throws it. **The instance the question was asked with**,
    /// and not a probe made and dropped before it: a driver's start is a tenth of a second, which every
    /// death test's child paid twice.
    Result<std::unique_ptr<Instance>, std::string> makeInstance(const ValidationOptions& options);
}
