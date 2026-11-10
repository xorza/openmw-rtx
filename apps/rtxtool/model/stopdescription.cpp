#include "stopdescription.hpp"

#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstddef>
#include <format>
#include <span>
#include <string>
#include <string_view>

#include <osg/Math>
#include <osg/Vec3f>

#include <components/rtx/world/weather.hpp>

#include "benchrecord.hpp"

namespace RtxTool
{
    namespace
    {
        /// A view id derived from a cell's name, for a window that was opened without one.
        ///
        /// Something to paste rather than something to keep: the ids in the file are chosen to say
        /// what a view is *for*, which a cell name cannot.
        std::string slugOf(std::string_view cell)
        {
            std::string slug;
            for (const char letter : cell)
            {
                const bool plain = (letter >= 'a' && letter <= 'z') || (letter >= 'A' && letter <= 'Z')
                    || (letter >= '0' && letter <= '9');
                if (plain)
                    slug += static_cast<char>(std::tolower(static_cast<unsigned char>(letter)));
                else if (!slug.empty() && slug.back() != '-')
                    slug += '-';
            }

            while (!slug.empty() && slug.back() == '-')
                slug.pop_back();

            return slug.empty() ? "new-view" : slug;
        }
    }

    float bearingOf(const Stand& stand)
    {
        const float degrees = osg::RadiansToDegrees(stand.getRotation().z());
        return degrees < 0.0f ? degrees + 360.0f : degrees;
    }

    float climbOf(const Stand& stand)
    {
        return osg::RadiansToDegrees(-stand.getRotation().x());
    }

    std::string describeSpot(const Stop& stop)
    {
        const osg::Vec3f& eye = *stop.mStand.mEye;

        return std::format("# {} at {:.0f}, {:.0f}, {:.0f} — bearing {:.0f}°, climb {:.0f}° — day {}, {}, {}\n",
            stop.mStand.mCell.getSpelling(), eye.x(), eye.y(), eye.z(), bearingOf(stop.mStand), climbOf(stop.mStand),
            stop.mSky.mDay.value_or(0), describeHour(stop.mSky.mHour.value_or(sDefaultHour)),
            Rtx::nameOf(stop.mSky.mWeather.value_or(sDefaultWeather)));
    }

    std::string describeAir(const Rtx::AirClock& air)
    {
        return std::format("{},{},{},{}", air.mSky.mSeconds, air.mSky.mCloudScroll, air.mCarried.x(), air.mCarried.y());
    }

    std::string describeId(const Stop& stop)
    {
        return slugOf(stop.mName);
    }

    std::string describeBlock(const Stop& stop)
    {
        std::string block = std::format("[{}]\n", describeId(stop));

        if (!stop.mNote.empty())
            block += std::format("note = {}\n", stop.mNote);

        const osg::Vec3f& eye = *stop.mStand.mEye;
        const osg::Vec3f look = stop.mStand.getLook();
        block += std::format("cell = {}\npos = {}, {}, {}\nlook = {}, {}, {}\n", stop.mStand.mCell.getSpelling(),
            eye.x(), eye.y(), eye.z(), look.x(), look.y(), look.z());

        // **Each condition only where the window was not at the file's own**, because one written
        // down fixes the place under it. A block pasted from a window flown at dawn in a storm has
        // to bring both with it — the light is most of what the frame is — and one from a window at
        // clear noon should leave the view free to be measured under whatever a run names.
        if (stop.mSky.mHour.has_value() && *stop.mSky.mHour != sDefaultHour)
            block += std::format("hour = {}\n", *stop.mSky.mHour);

        if (stop.mSky.mWeather.has_value() && *stop.mSky.mWeather != sDefaultWeather)
            block += std::format("weather = {}\n", Rtx::nameOf(*stop.mSky.mWeather));

        if (!stop.mStand.mLamps)
            block += "lamps = false\n";

        if (!stop.mStand.mArms.empty())
            block += std::format("arms = {}\n", stop.mStand.mArms);

        // Always where it is known, since no air is the file's own: one left out is whatever the
        // run's frames carried it to.
        if (stop.mSky.mAir.has_value())
            block += std::format("air = {}\n", describeAir(*stop.mSky.mAir));

        return block;
    }

    std::string describeCommand(const Stop& stop)
    {
        const osg::Vec3f& eye = *stop.mStand.mEye;
        const osg::Vec3f look = stop.mStand.getLook();

        // Quoted, because an interior's name has spaces and commas in it; `=` on every switch,
        // because a leading minus reads as an option otherwise, as `--pos`'s help says.
        std::string command = std::format(
            "# openmw-rtxtool view --cell=\"{}\" --pos={},{},{} --look={},{},{} --hour={} --day={} --weather={}",
            stop.mStand.mCell.getSpelling(), eye.x(), eye.y(), eye.z(), look.x(), look.y(), look.z(),
            stop.mSky.mHour.value_or(sDefaultHour), stop.mSky.mDay.value_or(0),
            Rtx::nameOf(stop.mSky.mWeather.value_or(sDefaultWeather)));

        if (stop.mSky.mAir.has_value())
            command += std::format(" --air={}", describeAir(*stop.mSky.mAir));

        command += '\n';
        return command;
    }

    std::string describeKey(const Stop& stop)
    {
        return describeBlock(stop) + std::format("day = {}\n", stop.mSky.mDay.value_or(0));
    }

    std::string describeStanding(const Stop& stop)
    {
        return describeSpot(stop) + describeBlock(stop) + describeCommand(stop);
    }

    std::string_view writeSkyNote(const std::span<char> room, const SkyNote& note)
    {
        // `describeHour`'s minute, so the title and the block a window prints agree on it.
        const int minutes = minuteOfDay(note.mHour);

        // Cut down and never rounded up: a hundred means arrived.
        const int percent = static_cast<int>(note.mCrossed * 100.0f);

        const auto [end, length] = note.mArriving.empty()
            ? std::format_to_n(room.data(), room.size(), "{}, {:02}:{:02}", note.mWeather, minutes / 60, minutes % 60)
            : std::format_to_n(room.data(), room.size(), "{} → {} {}%, {:02}:{:02}", note.mWeather, note.mArriving,
                  percent, minutes / 60, minutes % 60);

        assert(static_cast<std::size_t>(length) <= room.size() && "the sky note outgrew its room");
        return std::string_view(room.data(), std::min(static_cast<std::size_t>(length), room.size()));
    }
}
