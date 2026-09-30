#include "blockfile.hpp"

#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <format>
#include <fstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>

#include <components/files/conversion.hpp>
#include <components/rtx/environment/skylight.hpp>

#include "benchrun.hpp"

namespace RtxTool
{
    namespace
    {
        template <class Number>
        std::optional<Number> parseNumber(std::string_view text)
        {
            Number value = 0;
            const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
            // `from_chars` reads "inf" and "nan", which no field of a view or a bench stands for.
            if (error != std::errc() || end != text.data() + text.size() || !std::isfinite(value))
                return std::nullopt;

            return value;
        }

        /// `text` cut at its commas into exactly `into.size()` pieces, each trimmed, or false where
        /// it has any other number of them.
        bool splitExactly(std::string_view text, std::span<std::string_view> into)
        {
            for (std::size_t at = 0; at < into.size(); ++at)
            {
                const bool last = at + 1 == into.size();
                const std::size_t comma = text.find(',');
                if ((comma == std::string_view::npos) != last)
                    return false;

                into[at] = trimmed(text.substr(0, comma));
                if (!last)
                    text.remove_prefix(comma + 1);
            }

            return true;
        }
    }

    std::optional<float> parseFloat(std::string_view text)
    {
        return parseNumber<float>(text);
    }

    std::optional<osg::Vec3f> parseVec3(std::string_view text)
    {
        std::array<std::string_view, 3> pieces;
        if (!splitExactly(text, pieces))
            return std::nullopt;

        osg::Vec3f result;
        for (std::size_t axis = 0; axis < pieces.size(); ++axis)
        {
            const std::optional<float> value = parseFloat(pieces[axis]);
            if (!value.has_value())
                return std::nullopt;

            result[static_cast<int>(axis)] = *value;
        }

        return result;
    }

    std::optional<Rtx::AirClock> parseAir(std::string_view text)
    {
        std::array<std::string_view, 4> pieces;
        if (!splitExactly(text, pieces))
            return std::nullopt;

        const std::optional<double> seconds = parseNumber<double>(pieces[0]);
        const std::optional<float> scroll = parseNumber<float>(pieces[1]);
        const std::optional<double> x = parseNumber<double>(pieces[2]);
        const std::optional<double> y = parseNumber<double>(pieces[3]);
        if (!seconds.has_value() || !scroll.has_value() || !x.has_value() || !y.has_value())
            return std::nullopt;

        // The ranges the clocks keep: `Sky::SkyClock::step` only adds to the seconds and wraps the
        // scroll at four, and a carry is a sum of finite steps.
        if (!(*seconds >= 0.0) || !(*scroll >= 0.0f) || !(*scroll < 4.0f))
            return std::nullopt;

        return Rtx::AirClock{ .mSky = { .mSeconds = *seconds, .mCloudScroll = *scroll }, .mCarried = { *x, *y } };
    }

    std::string_view trimmed(std::string_view text)
    {
        const auto blank = [](char c) { return c == ' ' || c == '\t' || c == '\r'; };
        while (!text.empty() && blank(text.front()))
            text.remove_prefix(1);
        while (!text.empty() && blank(text.back()))
            text.remove_suffix(1);
        return text;
    }

    BlockFile::BlockFile(std::istream& in, std::string source)
        : mSource(std::move(source))
    {
        std::size_t number = 0;
        for (std::string line; std::getline(in, line);)
        {
            ++number;
            const std::string_view text = trimmed(line);
            if (text.empty() || text.front() == '#')
                continue;

            if (text.front() == '[')
            {
                if (text.back() != ']')
                    refuse(number, "a section's name is not closed by ]");

                mBlocks.push_back(
                    Block{ .mName = std::string(trimmed(text.substr(1, text.size() - 2))), .mLine = number });
                continue;
            }

            const std::size_t equals = text.find('=');
            if (equals == std::string_view::npos)
                refuse(number, std::format("\"{}\" is neither a [section], a field = value, nor a # comment", text));
            if (mBlocks.empty())
                refuse(number, "a field comes before the first [section]");

            mBlocks.back().mFields.push_back(BlockField{ .mName = std::string(trimmed(text.substr(0, equals))),
                .mValue = std::string(trimmed(text.substr(equals + 1))),
                .mLine = number });
        }
    }

    BlockFile BlockFile::load(const std::filesystem::path& path)
    {
        std::ifstream in(path);
        if (!in)
            throw std::runtime_error("cannot read " + Files::pathToUnicodeString(path));

        return BlockFile(in, Files::pathToUnicodeString(path));
    }

    void BlockFile::refuse(const std::size_t line, const std::string_view why) const
    {
        throw std::runtime_error(std::format("{}:{}: {}", mSource, line, why));
    }

    void BlockFile::refuseValue(const BlockField& field, const std::string_view what) const
    {
        refuse(field.mLine, std::format("{} \"{}\" {}", field.mName, field.mValue, what));
    }

    void BlockFile::refuseUnknown(const BlockField& field, const std::string_view kind) const
    {
        refuse(field.mLine, std::format("a {} has no field called \"{}\"", kind, field.mName));
    }

    void BlockFile::refuseRepeat(const Block& block, const Block& first, const std::string_view kind) const
    {
        refuse(block.mLine,
            std::format("a second {} called \"{}\", which line {} already defines", kind, block.mName, first.mLine));
    }

    float BlockFile::number(const BlockField& field) const
    {
        const std::optional<float> value = parseFloat(field.mValue);
        if (!value.has_value())
            refuseValue(field, "is not a number");

        return *value;
    }

    float BlockFile::positive(const BlockField& field, const std::string_view what) const
    {
        const float value = number(field);
        if (!(value > 0.0f))
            refuseValue(field, std::format("is not {}", what));

        return value;
    }

    float BlockFile::notNegative(const BlockField& field, const std::string_view what) const
    {
        const float value = number(field);
        if (!(value >= 0.0f))
            refuseValue(field, std::format("is not {}", what));

        return value;
    }

    std::optional<std::string_view> hourRefusal(const float hour)
    {
        if (!(hour >= 0.0f) || !(hour < 24.0f))
            return "is not from 0 up to but not including 24";

        return std::nullopt;
    }

    std::optional<std::string_view> dayRefusal(const int day)
    {
        if (day < 0)
            return "is before the first day";

        return std::nullopt;
    }

    std::optional<std::string_view> weatherRefusal(const std::string_view weather)
    {
        // **Checked where it is read rather than at the frame**, for the reason a mistyped view id
        // is: a place that quietly stood under another sky reports a number against a frame nobody
        // asked for.
        if (!Rtx::weatherIndex(weather).has_value())
            return "is none of the weathers the content files name";

        return std::nullopt;
    }

    std::string listWeathers()
    {
        std::string list;
        for (std::uint32_t weather = 0; !Rtx::weatherName(weather).empty(); ++weather)
        {
            if (!list.empty())
                list += ", ";
            list += Rtx::weatherName(weather);
        }

        return list;
    }

    float BlockFile::hour(const BlockField& field) const
    {
        const float value = number(field);
        if (const std::optional<std::string_view> why = hourRefusal(value))
            refuseValue(field, *why);

        return value;
    }

    std::string BlockFile::weather(const BlockField& field) const
    {
        const std::optional<std::uint32_t> named = Rtx::weatherIndex(field.mValue);
        if (!named.has_value())
            refuseValue(field, *weatherRefusal(field.mValue));

        return std::string(Rtx::weatherName(*named));
    }

    osg::Vec3f BlockFile::point(const BlockField& field) const
    {
        const std::optional<osg::Vec3f> value = parseVec3(field.mValue);
        if (!value.has_value())
            refuseValue(field, "is not three numbers separated by commas");

        return *value;
    }

    Rtx::AirClock BlockFile::air(const BlockField& field) const
    {
        const std::optional<Rtx::AirClock> value = parseAir(field.mValue);
        if (!value.has_value())
            refuseValue(field,
                "is not the sky's seconds from nought, the deck's scroll from nought up to but not including "
                "four, and the drift's two coordinates, separated by commas");

        return *value;
    }

    bool BlockFile::boolean(const BlockField& field) const
    {
        if (field.mValue != "true" && field.mValue != "false")
            refuseValue(field, "is not true or false");

        return field.mValue == "true";
    }

    int BlockFile::day(const BlockField& field) const
    {
        const std::string& text = field.mValue;
        int value = 0;
        const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
        if (error != std::errc() || end != text.data() + text.size())
            refuseValue(field, "is not a whole number of days");
        if (const std::optional<std::string_view> why = dayRefusal(value))
            refuseValue(field, *why);

        return value;
    }

    bool BlockFile::readPlace(const BlockField& field, Stop& stop) const
    {
        if (field.mName == "cell")
            stop.mStand.mCell = field.mValue;
        else if (field.mName == "pos")
            stop.mStand.mEye = point(field);
        else if (field.mName == "look")
            stop.mStand.mLook = point(field);
        else if (field.mName == "note")
            stop.mNote = field.mValue;
        else if (field.mName == "hour")
            stop.mSky.mHour = hour(field);
        else if (field.mName == "weather")
            stop.mSky.mWeather = weather(field);
        else if (field.mName == "air")
            stop.mSky.mAir = air(field);
        else
            return false;

        return true;
    }
}
