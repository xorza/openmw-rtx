#include "validation.hpp"

#include <algorithm>
#include <cassert>
#include <iterator>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

#include <vulkan/vk_platform.h>

#include <components/crashcatcher/crash.hpp>
#include <components/debug/debuglog.hpp>

namespace Rtx
{
    namespace
    {
        VKAPI_ATTR VkBool32 VKAPI_CALL onMessage(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
            VkDebugUtilsMessageTypeFlagsEXT types, const VkDebugUtilsMessengerCallbackDataEXT* data, void* userData)
        {
            ValidationLog& log = *static_cast<ValidationLog*>(userData);
            const char* const id = data->pMessageIdName != nullptr ? data->pMessageIdName : "?";

            // **Only the layers' own messages are validation errors.** The loader reports through the
            // same messenger with the general type alone: "Failed to detect any valid GPUs" is an
            // error there, and aborting on it left a machine with no device short of the answer
            // `PhysicalDevice::select` gives it.
            if ((severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) != 0
                && (types & VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT) == 0)
                Log(Debug::Error) << "Vulkan: " << id << ": " << data->pMessage;
            else if ((severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) != 0
                && std::string_view(id).starts_with("SYNC-HAZARD")
                && log.isDisowned(std::span(data->pObjects, data->objectCount)))
                Log(Debug::Warning) << "Vulkan validation, a runtime's own resources: " << id << ": " << data->pMessage;
            else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
            {
                std::string text(id);
                text += ": ";
                text += data->pMessage;
                Log(Debug::Error) << "Vulkan validation: " << text;

                // **A crash whose report says what the layers said**, cut to a note's length, where an
                // abort's said nothing but `SIGABRT`; the whole message is in the log above.
                if (log.abortsOnError())
                    Crash::fatal("Vulkan validation: " + text);
                log.recordError(std::move(text));
            }
            else
                Log(Debug::Warning) << "Vulkan validation: " << id << ": " << data->pMessage;

            // The spec reserves a true return for the layers' own use; applications must return false.
            return VK_FALSE;
        }
    }

    void ValidationLog::recordError(std::string&& text)
    {
        const std::scoped_lock lock(mMutex);
        const std::thread::id self = std::this_thread::get_id();
        const auto adopted = std::ranges::find(mAdopted.rbegin(), mAdopted.rend(), self, &Adoption::mThread);
        mErrors.push_back(ValidationMessage{ std::move(text), adopted != mAdopted.rend() ? adopted->mOwner : self });
    }

    void ValidationLog::disown(std::string prefix)
    {
        assert(!prefix.empty() && "a prefix every name starts with disowns every resource");
        const std::scoped_lock lock(mMutex);
        mDisowned.push_back(std::move(prefix));
    }

    bool ValidationLog::isDisowned(const std::span<const VkDebugUtilsObjectNameInfoEXT> objects) const
    {
        const std::scoped_lock lock(mMutex);
        bool any = false;
        for (const VkDebugUtilsObjectNameInfoEXT& object : objects)
        {
            if (object.objectType != VK_OBJECT_TYPE_IMAGE && object.objectType != VK_OBJECT_TYPE_BUFFER)
                continue;
            const std::string_view name = object.pObjectName != nullptr ? object.pObjectName : "";
            if (std::ranges::none_of(mDisowned, [&](const std::string& prefix) { return name.starts_with(prefix); }))
                return false;
            any = true;
        }
        return any;
    }

    void ValidationLog::takeErrorsOnThisThread(std::vector<std::string>& out)
    {
        const std::thread::id current = std::this_thread::get_id();

        const std::scoped_lock lock(mMutex);

        // Stable, so that messages come back in the order the layers raised them: the first is
        // usually the mistake and the rest are what it led to.
        const auto taken = std::stable_partition(mErrors.begin(), mErrors.end(),
            [current](const ValidationMessage& message) { return message.mThread != current; });

        for (auto at = taken; at != mErrors.end(); ++at)
            out.push_back(std::move(at->mText));

        mErrors.erase(taken, mErrors.end());
    }

    void ValidationLog::adopt(const std::thread::id owner)
    {
        const std::scoped_lock lock(mMutex);
        mAdopted.push_back(Adoption{ .mThread = std::this_thread::get_id(), .mOwner = owner });
    }

    void ValidationLog::release()
    {
        const std::scoped_lock lock(mMutex);
        const auto adopted
            = std::ranges::find(mAdopted.rbegin(), mAdopted.rend(), std::this_thread::get_id(), &Adoption::mThread);
        assert(adopted != mAdopted.rend() && "an adoption released that this thread never made");
        mAdopted.erase(std::next(adopted).base());
    }

    AdoptedThread::AdoptedThread(ValidationLog* const log, const std::thread::id owner)
        : mLog(log)
    {
        if (mLog != nullptr)
            mLog->adopt(owner);
    }

    AdoptedThread::~AdoptedThread()
    {
        if (mLog != nullptr)
            mLog->release();
    }

    VkDebugUtilsMessengerCreateInfoEXT makeMessengerCreateInfo(ValidationLog& log)
    {
        return VkDebugUtilsMessengerCreateInfoEXT{
            .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
            .pNext = nullptr,
            .flags = 0,
            // Errors and warnings only. Info severity is where the loader narrates every manifest it
            // reads, which buries the two severities anyone acts on. VK_LOADER_DEBUG covers that case.
            .messageSeverity
            = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT,
            .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT
                | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
            .pfnUserCallback = &onMessage,
            .pUserData = &log,
        };
    }
}
