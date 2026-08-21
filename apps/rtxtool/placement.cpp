#include "placement.hpp"

#include <cmath>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <string>

#include <osg/Math>

namespace RtxTool
{
    Placement placeCamera(const osg::BoundingBoxf& bounds, float verticalFovDegrees,
        const std::optional<osg::Vec3f>& origin, const std::optional<osg::Vec3f>& target)
    {
        const osg::Vec3f centre = bounds.center();

        osg::Vec3f direction(0.6f, 0.6f, 0.35f);
        direction.normalize();

        // Far enough back that the bounding sphere fits the vertical field of view, and a little
        // further so it is not touching the edges.
        const float distance = bounds.radius() / std::tan(osg::DegreesToRadians(verticalFovDegrees) * 0.5f) * 1.15f;

        return Placement{
            .mOrigin = origin.value_or(centre + direction * distance),
            .mTarget = target.value_or(centre),
        };
    }

    std::optional<osg::Vec3f> parseVec3(std::string_view text, std::string_view what)
    {
        if (text.empty())
            return std::nullopt;

        const auto fail = [&] {
            throw std::runtime_error(
                std::string(what) + " is not three numbers separated by commas: \"" + std::string(text) + '"');
        };

        osg::Vec3f result;
        for (int axis = 0; axis < 3; ++axis)
        {
            while (!text.empty() && text.front() == ' ')
                text.remove_prefix(1);

            const std::size_t comma = text.find(',');
            const std::string_view field = text.substr(0, comma);

            // Not `std::from_chars`: libc++ ships the floating-point overload only from macOS 26,
            // and this is a command line, where trailing rubbish has to be rejected rather than
            // quietly ignored. The classic locale is what keeps the decimal separator a point
            // wherever this runs, and `eof` is what says the whole field was consumed — the same
            // question `from_chars` answers with its end pointer.
            std::istringstream stream{ std::string(field) };
            stream.imbue(std::locale::classic());
            if (!(stream >> result[axis]) || !stream.eof())
                fail();

            const bool last = axis == 2;
            if ((comma == std::string_view::npos) != last)
                fail();

            if (!last)
                text = text.substr(comma + 1);
        }

        return result;
    }
}
