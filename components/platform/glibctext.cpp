#include "glibctext.hpp"

#include <charconv>
#include <cstdint>
#include <system_error>
#include <vector>

namespace Platform::GlibcText
{
    namespace
    {
        /// The `size` a line of `text` that begins with `tag` states, `<tag ... size="<number>"/>`, or nothing where no
        /// line does or its size is not a number.
        std::optional<std::uint64_t> sizeOf(std::string_view text, std::string_view tag)
        {
            const std::size_t at = text.find(tag);
            if (at == std::string_view::npos)
                return std::nullopt;
            std::string_view line = text.substr(at);
            line = line.substr(0, line.find('\n'));
            constexpr std::string_view sSize = "size=\"";
            const std::size_t size = line.find(sSize);
            if (size == std::string_view::npos)
                return std::nullopt;
            line.remove_prefix(size + sSize.size());
            std::uint64_t value = 0;
            const std::from_chars_result read = std::from_chars(line.data(), line.data() + line.size(), value);
            if (read.ec != std::errc{} || read.ptr == line.data() + line.size() || *read.ptr != '"')
                return std::nullopt;
            return value;
        }

        /// The free bytes a line of `arena` that begins with `tag` states, nought where none does, as glibc writes no
        /// line of a kind of free chunk an arena has none of; nothing where its size is not a number.
        std::optional<std::uint64_t> freeOf(std::string_view arena, std::string_view tag)
        {
            return arena.find(tag) == std::string_view::npos ? std::optional<std::uint64_t>(0) : sizeOf(arena, tag);
        }
    }

    std::optional<HeapHeld> heapHeld(std::string_view report)
    {
        constexpr std::string_view sHeap = "<heap nr=\"";
        constexpr std::string_view sHeapEnd = "</heap>";

        HeapHeld held;
        std::uint64_t free = 0;
        std::string_view rest = report;
        for (std::size_t at = rest.find(sHeap); at != std::string_view::npos; at = rest.find(sHeap))
        {
            const std::size_t end = rest.find(sHeapEnd, at);
            if (end == std::string_view::npos)
                return std::nullopt;
            const std::string_view arena = rest.substr(at, end - at);
            rest.remove_prefix(end + sHeapEnd.size());

            const std::optional<std::uint64_t> fast = freeOf(arena, "<total type=\"fast\"");
            const std::optional<std::uint64_t> other = freeOf(arena, "<total type=\"rest\"");
            const std::optional<std::uint64_t> taken = sizeOf(arena, "<system type=\"current\"");
            if (!fast.has_value() || !other.has_value() || !taken.has_value() || *fast + *other > *taken)
                return std::nullopt;
            held.mArenas.push_back(HeapArena{ .mTaken = *taken, .mFree = *fast + *other });
            held.mTaken += *taken;
            free += *fast + *other;
        }
        if (held.mArenas.empty())
            return std::nullopt;

        // After the last arena, the heap's own totals, of which the chunks mapped on their own are no arena's.
        const std::optional<std::uint64_t> mapped = sizeOf(rest, "<total type=\"mmap\"");
        if (!mapped.has_value())
            return std::nullopt;
        held.mTaken += *mapped;
        held.mInUse = held.mTaken - free;
        return held;
    }

    bool namesTheHeapsSettings(std::string_view tunables)
    {
        while (!tunables.empty())
        {
            const std::size_t end = tunables.find(':');
            const std::string_view entry = tunables.substr(0, end);
            if (entry.starts_with("glibc.malloc.arena_max=") || entry.starts_with("glibc.malloc.mmap_threshold="))
                return true;
            if (end == std::string_view::npos)
                break;
            tunables.remove_prefix(end + 1);
        }
        return false;
    }
}
