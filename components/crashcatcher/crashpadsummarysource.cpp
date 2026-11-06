#include "crashpadsummarysource.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <minidump/minidump_user_extension_stream_data_source.h>
#include <snapshot/cpu_context.h>
#include <snapshot/exception_snapshot.h>
#include <snapshot/memory_snapshot.h>
#include <snapshot/module_snapshot.h>
#include <snapshot/process_snapshot.h>
#include <snapshot/thread_snapshot.h>
#include <util/misc/uuid.h>
#include <util/process/process_memory.h>

#include <components/files/conversion.hpp>
#include <components/platform/userhome.hpp>

#include "crashmonitorlog.hpp"
#include "crashnote.hpp"
#include "crashpadmonitorsystem.hpp"

namespace Crash
{
    namespace
    {
        /// The module an address lies in, and the offset in it: "openmw.exe+0x112a9a7".
        std::string locate(const std::vector<const crashpad::ModuleSnapshot*>& modules, std::uint64_t address)
        {
            for (const crashpad::ModuleSnapshot* module : modules)
                if (address >= module->Address() && address - module->Address() < module->Size())
                {
                    const std::string path = module->Name();
                    const std::size_t slash = path.find_last_of("/\\");
                    return (slash == std::string::npos ? path : path.substr(slash + 1)) + "+"
                        + hex(address - module->Address());
                }

            return {};
        }

        class Collect final : public crashpad::MemorySnapshot::Delegate
        {
        public:
            explicit Collect(std::vector<std::uint8_t>& into)
                : mInto(into)
            {
            }

            bool MemorySnapshotDelegateRead(void* data, size_t size) override
            {
                const auto* const bytes = static_cast<const std::uint8_t*>(data);
                mInto.assign(bytes, bytes + size);
                return true;
            }

        private:
            std::vector<std::uint8_t>& mInto;
        };

        /// Values on the faulting thread's stack, from its stack pointer up, that point into a
        /// module: the return addresses among them, and some that only look like one.
        void scanStack(const crashpad::ProcessSnapshot& snapshot, std::uint64_t thread,
            const crashpad::CPUContext& context, std::vector<std::string>& into)
        {
            constexpr std::size_t sCandidates = 16;

            for (const crashpad::ThreadSnapshot* one : snapshot.Threads())
            {
                const crashpad::MemorySnapshot* const stack = one->ThreadID() == thread ? one->Stack() : nullptr;
                if (stack == nullptr)
                    continue;

                std::vector<std::uint8_t> bytes;
                Collect collect(bytes);
                if (!stack->Read(&collect))
                    return;

                const std::size_t word = context.Is64Bit() ? 8 : 4;
                const std::uint64_t sp = context.StackPointer();
                std::size_t at = sp >= stack->Address() && sp - stack->Address() < bytes.size()
                    ? static_cast<std::size_t>(sp - stack->Address())
                    : 0;
                at -= at % word;

                const std::vector<const crashpad::ModuleSnapshot*> modules = snapshot.Modules();
                for (; at + word <= bytes.size() && into.size() < sCandidates; at += word)
                {
                    std::uint64_t value = 0;
                    std::copy_n(bytes.data() + at, word, reinterpret_cast<std::uint8_t*>(&value));
                    // A value spilled twice in a row is one address, and names one frame at most.
                    if (std::string where = locate(modules, value);
                        !where.empty() && (into.empty() || into.back() != where))
                        into.push_back(std::move(where));
                }
                return;
            }
        }

        /// The registers `thread` stood at as the snapshot was taken, or null where it holds no such
        /// thread.
        const crashpad::CPUContext* contextOf(const crashpad::ProcessSnapshot& snapshot, std::uint64_t thread)
        {
            for (const crashpad::ThreadSnapshot* one : snapshot.Threads())
                if (one->ThreadID() == thread)
                    return one->Context();
            return nullptr;
        }

        class TextStream final : public crashpad::MinidumpUserExtensionStreamDataSource
        {
        public:
            explicit TextStream(std::string text)
                : MinidumpUserExtensionStreamDataSource(sSummaryStream)
                , mText(std::move(text))
            {
            }

            size_t StreamDataSize() override { return mText.size(); }

            bool ReadStreamData(Delegate* delegate) override
            {
                return delegate->ExtensionStreamDataSourceRead(mText.data(), mText.size());
            }

        private:
            std::string mText;
        };
    }

    void SessionReports::add(const crashpad::UUID& id, const bool crashed)
    {
        const std::lock_guard lock(mMutex);
        mSession.mIds.push_back(id);
        mSession.mCrashed = mSession.mCrashed || crashed;
    }

    void SessionReports::setLast(LastReport last)
    {
        const std::lock_guard lock(mMutex);
        mSession.mLast = std::move(last);
    }

    SessionReports::Taken SessionReports::take()
    {
        const std::lock_guard lock(mMutex);
        return std::move(mSession);
    }

    std::unique_ptr<crashpad::MinidumpUserExtensionStreamDataSource> SummarySource::ProduceStreamData(
        crashpad::ProcessSnapshot* snapshot)
    {
        crashpad::UUID report;
        snapshot->ReportID(&report);

        // **The dump is written whatever the summary comes to**: this runs inside Crashpad's
        // handler, which a throw ends with the dump unwritten and the game left waiting on
        // it, and the monitor that reports a crash must not become one. What went wrong
        // stands where the summary would have.
        CrashFacts facts;
        std::string text;
        std::string failure;
        try
        {
            text = describe(*snapshot, report.ToString() + ".dmp", facts);
        }
        catch (const std::exception& thrown)
        {
            failure = thrown.what();
        }
        catch (...)
        {
            failure = "an exception that is no std::exception";
        }

        // A report summarised in part is not left to an issue filled in with the report
        // before it, and the public reads that it failed and not why, since an error's
        // message names whatever path it was handed.
        constexpr std::string_view unsummarised = "Crash: the monitor could not summarise this report";
        if (!failure.empty())
        {
            text = std::string(unsummarised) + ": " + failure;
            mParts.mLog.write({ text });
            text += '\n';
        }

        mParts.mReports.add(report, facts.mNotes.mKind == ReportKind::Crash);
        if (!failure.empty())
            mParts.mReports.setLast(LastReport{ std::string(unsummarised), { std::string(unsummarised) } });
        return std::make_unique<TextStream>(std::move(text));
    }

    std::string SummarySource::describe(
        const crashpad::ProcessSnapshot& process, const std::string& dump, CrashFacts& facts)
    {
        const crashpad::ExceptionSnapshot* const exception = process.Exception();
        const std::uint64_t asked = exception != nullptr ? exception->ThreadID() : 0;
        facts.mStalledFor = mParts.mStalledFor.load();
        if (exception != nullptr)
            facts.mException = Monitor::describeException(*exception, mParts.mArguments.mClient);

        // The exception says what the dump is, and the table only what a dump asked for is:
        // `describeException` names every fault and nothing the game asked for.
        std::vector<std::byte> table(noteTable().size());
        const crashpad::ProcessMemory* const memory = process.Memory();
        const bool read = memory != nullptr && mParts.mArguments.mNotes != 0
            && memory->Read(mParts.mArguments.mNotes, table.size(), table.data());
        Heartbeat* const page = mParts.mPage.get();
        const std::uint64_t drawing = page != nullptr ? std::atomic_ref(page->mDrawing).load() : 0;
        readNotes(read ? std::span<const std::byte>(table) : std::span<const std::byte>(), asked, drawing,
            !facts.mException.empty(), facts.mNotes);

        // A hang's report is about the thread that draws, whose context is its own: the
        // exception's is the thread's that took the monitor's request.
        const std::uint64_t thread = facts.mNotes.mThread;
        const crashpad::CPUContext* const context
            = exception != nullptr && thread == asked ? exception->Context() : contextOf(process, thread);
        if (context != nullptr)
        {
            facts.mWhere = locate(process.Modules(), context->InstructionPointer());
            scanStack(process, thread, *context, facts.mStack);
        }

        for (const auto& [key, value] : process.AnnotationsSimpleMap())
            facts.mAnnotations.emplace_back(key, value);
        for (const crashpad::ModuleSnapshot* module : process.Modules())
            for (const auto& [key, value] : module->AnnotationsSimpleMap())
                facts.mAnnotations.emplace_back(key, value);

        facts.mDump = dump;

        std::vector<std::string> lines;
        summarise(facts, lines, SummaryReaders{});
        mParts.mLog.write(lines);

        const std::optional<std::filesystem::path> home = Platform::userHome();
        const SummaryReaders anybody{
            .mPublic = true,
            .mHome = home.has_value() ? Files::pathToUnicodeString(*home) : std::string(),
        };
        std::vector<std::string> shared;
        summarise(facts, shared, anybody);

        mParts.mReports.setLast(LastReport{ title(facts, anybody), std::move(shared) });

        std::string text;
        for (const std::string& line : lines)
            text += line + '\n';
        return text;
    }
}
