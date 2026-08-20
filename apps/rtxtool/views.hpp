#ifndef OPENMW_APPS_RTXTOOL_VIEWS_H
#define OPENMW_APPS_RTXTOOL_VIEWS_H

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <osg/Vec3f>

namespace RtxTool
{
    /// A place worth looking at, by name.
    ///
    /// A view id is the unit of comparison across commits: the same name renders the same frame
    /// today and after a change, which is what makes a screenshot evidence rather than an anecdote.
    struct View
    {
        std::string mName;

        /// Addressed the way Morrowind does: a pair of integers is an exterior, anything else is an
        /// interior's name.
        std::string mCell;

        /// Left out for a view that only names a cell, which then gets the default placement.
        std::optional<osg::Vec3f> mOrigin;
        std::optional<osg::Vec3f> mTarget;

        std::string mNote;
    };

    /// Reads the view file. Throws when it is missing or malformed — a mistyped view should say so
    /// rather than quietly render somewhere else.
    std::vector<View> loadViews(const std::filesystem::path& path);

    /// The view called `name`, or null.
    const View* findView(const std::vector<View>& views, std::string_view name);
}

#endif
