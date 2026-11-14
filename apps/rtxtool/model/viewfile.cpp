#include "viewfile.hpp"

#include <algorithm>
#include <cstddef>
#include <format>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <components/rtx/world/weather.hpp>

#include "blockfile.hpp"

namespace RtxTool
{
    std::vector<BenchSuite> loadSuites(const std::filesystem::path& path)
    {
        return readSuites(BlockFile::load(path));
    }

    std::vector<BenchSuite> readSuites(const BlockFile& file)
    {
        const std::span<const Block> blocks = file.getBlocks();

        std::vector<BenchSuite> suites;
        suites.reserve(blocks.size());
        for (std::size_t at = 0; at < blocks.size(); ++at)
        {
            const Block& block = blocks[at];
            for (std::size_t before = 0; before < at; ++before)
                if (blocks[before].mName == block.mName)
                    file.refuseRepeat(block, blocks[before], "suite");

            BenchSuite& suite = suites.emplace_back(BenchSuite{ .mName = block.mName });
            for (const BlockField& field : block.mFields)
            {
                if (field.mName == "views")
                    suite.mViews = splitNames(field.mValue);
                else if (field.mName == "note")
                    suite.mNote = field.mValue;
                else if (field.mName == "settled")
                    suite.mSettled = file.boolean(field);
                else
                    file.refuseUnknown(field, "suite");
            }

            if (suite.mViews.empty())
                file.refuse(block.mLine, std::format("suite \"{}\" names no views", suite.mName));
        }

        if (suites.empty())
            throw std::runtime_error(file.getSource() + " defines no suites");

        return suites;
    }

    const BenchSuite* findSuite(const std::vector<BenchSuite>& suites, std::string_view name)
    {
        const auto found
            = std::find_if(suites.begin(), suites.end(), [&](const BenchSuite& s) { return s.mName == name; });

        return found == suites.end() ? nullptr : &*found;
    }

    namespace
    {
        /// A cell's name as a stop's name, which a hash file's row and a picture's file are keyed on: the name as it
        /// is, but each character a row or a file name cannot hold as `_`. `-2,-9` is `-2_-9`, which `slugOf`'s
        /// `2-9` would share with `2,9`.
        std::string stopNameOf(std::string_view cell)
        {
            std::string name(cell);
            for (char& letter : name)
                if (std::string_view(",:/\\*?\"<>|").find(letter) != std::string_view::npos)
                    letter = '_';
            return name;
        }

        /// A field a view names another view by, `to` or `like` or the `speed` beside a `to`,
        /// waiting for every view to be read: the view that wrote it, and the field, for its line.
        struct Pending
        {
            std::size_t mView = 0;
            const BlockField* mField = nullptr;
        };

        /// Fills each borrower in from the view its `like` names. A place at another hour is the same place, and
        /// this keeps it so: a dawn row and a noon row of one camera mean something beside each other only where the
        /// camera is identical by construction, and coordinates copied by hand drift the first time either is moved.
        ///
        /// One level, and a route is not among what is taken: the view a `like` names states its own place, which
        /// leaves no chain to walk and no cycle to detect, and flying from a place is a different measurement
        /// rather than the same place under another light, so a borrower that wants one writes its own.
        void resolveLikes(const BlockFile& file, std::vector<Stop>& views, std::span<const Pending> likes)
        {
            for (const Pending& like : likes)
            {
                Stop& borrower = views[like.mView];
                const std::string& name = like.mField->mValue;
                if (borrower.mName == name)
                    file.refuse(like.mField->mLine, std::format("view \"{}\" is like itself", name));

                const auto lent = std::find_if(
                    likes.begin(), likes.end(), [&](const Pending& l) { return views[l.mView].mName == name; });
                if (lent != likes.end())
                    file.refuse(like.mField->mLine,
                        std::format("view \"{}\" is like \"{}\", which is itself like another view; only a view "
                                    "that states its own place may be lent",
                            borrower.mName, name));

                const Stop* source = findView(views, name);
                if (source == nullptr)
                    file.refuse(like.mField->mLine,
                        std::format("view \"{}\" is like \"{}\", which is not a view", borrower.mName, name));

                // Written through the vector while `source` points into it, which the check above
                // makes safe: the two are different views and nothing here resizes.
                if (borrower.mStand.mCell.empty())
                    borrower.mStand.mCell = source->mStand.mCell;
                if (!borrower.mStand.mEye.has_value())
                    borrower.mStand.mEye = source->mStand.mEye;
                if (!borrower.mStand.mLook.has_value())
                    borrower.mStand.mLook = source->mStand.mLook;
            }
        }

        /// Pairs each `to` with the view it names and with the `speed` beside it.
        ///
        /// Both halves are required and neither has a default: a route with no speed does not move and a speed with
        /// no destination has nowhere to go, and guessing what was meant is how a benchmark measures something other
        /// than what was asked for. The destination must also name its own `pos` and `look`, because a placement
        /// derived from a cell's bounds would need that cell staged to know it.
        void resolveRoutes(const BlockFile& file, std::vector<Stop>& views, std::span<const Pending> ends,
            std::span<const Pending> speeds)
        {
            const auto pairedWith = [](std::span<const Pending> list, std::size_t view) {
                return std::find_if(list.begin(), list.end(), [&](const Pending& p) { return p.mView == view; });
            };

            for (const Pending& speed : speeds)
                if (pairedWith(ends, speed.mView) == ends.end())
                    file.refuse(speed.mField->mLine,
                        std::format("view \"{}\" names a speed but nowhere to go", views[speed.mView].mName));

            for (const Pending& end : ends)
            {
                const std::string& to = end.mField->mValue;
                const auto speed = pairedWith(speeds, end.mView);
                if (speed == speeds.end())
                    file.refuse(end.mField->mLine,
                        std::format("view \"{}\" flies to \"{}\" at no speed", views[end.mView].mName, to));

                const Stop* arrival = findView(views, to);
                if (arrival == nullptr)
                    file.refuse(end.mField->mLine,
                        std::format("view \"{}\" flies to \"{}\", which is not a view", views[end.mView].mName, to));

                if (!arrival->mStand.mEye.has_value() || !arrival->mStand.mLook.has_value())
                    file.refuse(end.mField->mLine,
                        std::format("view \"{}\" flies to \"{}\", which names no pos and look of its own to arrive at",
                            views[end.mView].mName, to));

                views[end.mView].mSchedule.mRoute = Route{
                    .mTo = *arrival->mStand.mEye,
                    .mLookTo = *arrival->mStand.mLook,
                    .mSpeed = file.positive(*speed->mField, "a positive number of units a second"),
                };
            }
        }

        /// One condition, from what the command line named and what the view fixes. `stopFor`
        /// says which of the two wins and why.
        float hourFor(const std::optional<float>& given, const std::optional<float>& fixed)
        {
            return given.has_value() ? *given : fixed.value_or(sDefaultHour);
        }

        Rtx::Weather weatherFor(const std::optional<Rtx::Weather>& given, const std::optional<Rtx::Weather>& fixed)
        {
            return given.has_value() ? *given : fixed.value_or(sDefaultWeather);
        }
    }

    Stop stopFor(const Stop& view, const StopSky& given)
    {
        Stop stop = view;

        // **The cell where a view names no id**, because a report row and a hash file are keyed on
        // this and neither can be keyed on nothing.
        if (stop.mName.empty())
            stop.mName = stopNameOf(view.mStand.mCell.getSpelling());

        stop.mSky.mHour = hourFor(given.mHour, view.mSky.mHour);
        stop.mSky.mWeather = weatherFor(given.mWeather, view.mSky.mWeather);
        stop.mSky.mDay = given.mDay;
        if (given.mAir.has_value())
            stop.mSky.mAir = given.mAir;

        return stop;
    }

    std::vector<Stop> loadViews(const std::filesystem::path& path)
    {
        return readViews(BlockFile::load(path));
    }

    std::vector<Stop> readViews(const BlockFile& file)
    {
        const std::span<const Block> blocks = file.getBlocks();

        // Collected and resolved afterwards, because a route and a likeness can point forwards: `to` and `like` may
        // name a view that has not been read yet.
        std::vector<Pending> ends;
        std::vector<Pending> speeds;
        std::vector<Pending> likes;

        std::vector<Stop> views;
        views.reserve(blocks.size());
        for (std::size_t at = 0; at < blocks.size(); ++at)
        {
            const Block& block = blocks[at];
            for (std::size_t before = 0; before < at; ++before)
                if (blocks[before].mName == block.mName)
                    file.refuseRepeat(block, blocks[before], "view");

            // `chooseViews` reads the word as every view, so a view of that name could never be
            // chosen alone.
            if (block.mName == "all")
                file.refuse(block.mLine, "\"all\" names every view and may not name one");

            Stop& view = views.emplace_back(Stop{ .mName = block.mName });
            for (const BlockField& field : block.mFields)
            {
                if (file.readPlace(field, view))
                    continue;

                if (field.mName == "to")
                    ends.push_back(Pending{ .mView = at, .mField = &field });
                else if (field.mName == "speed")
                    speeds.push_back(Pending{ .mView = at, .mField = &field });
                else if (field.mName == "like")
                    likes.push_back(Pending{ .mView = at, .mField = &field });
                else
                    file.refuseUnknown(field, "view");
            }
        }

        if (views.empty())
            throw std::runtime_error(file.getSource() + " defines no views");

        // Before the cell is demanded and before a route is paired: a borrower takes both from what
        // it is like, and either check run first would reject a view that is about to be complete.
        resolveLikes(file, views, likes);

        for (std::size_t at = 0; at < views.size(); ++at)
        {
            if (views[at].mStand.mCell.empty())
                file.refuse(blocks[at].mLine, std::format("view \"{}\" names no cell", views[at].mName));

            // The body's own eye is put where a view's eye is, and one that names none leaves the
            // camera to the game, which would show no arms and say nothing.
            if (!views[at].mStand.mArms.empty() && !views[at].mStand.mEye.has_value())
                file.refuse(blocks[at].mLine, std::format("view \"{}\" names arms but no pos", views[at].mName));
        }

        resolveRoutes(file, views, ends, speeds);
        return views;
    }

    const Stop* findView(const std::vector<Stop>& views, std::string_view name)
    {
        const auto found = std::find_if(views.begin(), views.end(), [&](const Stop& v) { return v.mName == name; });
        return found == views.end() ? nullptr : &*found;
    }

    const Stop& requireView(const std::vector<Stop>& views, std::string_view name)
    {
        const Stop* view = findView(views, name);
        if (view == nullptr)
            throw std::runtime_error(std::format("no view is called \"{}\"; --list-views prints them", name));

        return *view;
    }

    std::vector<Stop> chooseViews(const std::vector<Stop>& views, const std::vector<std::string>& named)
    {
        // "all" is a name nothing may take, and it means every view; `bench` reaches this through a suite as well,
        // so the word has to mean the same on either road in. A list that names nothing, `--views=,`, is refused
        // rather than read as every view.
        if (named.empty())
            throw std::runtime_error("nothing to visit: no view was named");

        if (named.size() == 1 && named.front() == "all")
        {
            std::vector<Stop> every = views;
            for (Stop& view : every)
                view.mWhenAbsent = WhenAbsent::Skips;
            return every;
        }

        std::vector<Stop> chosen;
        chosen.reserve(named.size());
        for (const std::string& name : named)
        {
            const Stop& view = requireView(views, name);

            // A view named twice is one place visited twice under one name, and every table the
            // run writes by name — the frame hashes first — refuses it once written.
            if (findView(chosen, name) != nullptr)
                throw std::runtime_error("the view \"" + name + "\" is named twice");

            chosen.push_back(view);
        }

        return chosen;
    }
}
