#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <apps/openmw/mwrender/rtx/framereport.hpp>
#include <apps/rtxtool/instruments/frametimes.hpp>
#include <apps/rtxtool/model/benchrecord.hpp>
#include <apps/rtxtool/model/benchrun.hpp>
#include <components/rtx/renderer/renderer.hpp>
#include <components/rtx/world/weather.hpp>

#include "checks.hpp"
#include "picturemean.hpp"

namespace osg
{
    class Image;
}

namespace MWRender
{
    class OffscreenView;
}

namespace RtxTool
{
    class PictureShelf;
    class RunRecord;

    /// Writes what `Actions` asks of the place a stop stood at: what a stop produces, apart from what drove it
    /// there. A picture, its frame times, a scene report, a contact sheet, a map tile, a doll, a listing and a set
    /// of claims each need the renderer and a path, and none needs the schedule, the route, the camera or the sky.
    ///
    /// Held for the whole run for its read-back buffer alone: three of the writers land a frame in host memory,
    /// and a stop that allocates one is a stop measuring its own allocator.
    class StopWriter
    {
    public:
        /// Does everything `actions` asks of the frame `owner` last drew, and says what it came to
        /// in `record`.
        ///
        /// **The last measured frame, and never a later one.** Every figure the stop reports
        /// describes those frames, so a picture taken after them is a picture of a different run.
        ///
        /// @param facts what the stop asked for and came to, which only a check or the frame
        /// series reads.
        /// @param shelf where every picture written is kept as well, or null.
        void write(const MWRender::FrameContext& context, const MWRender::FrameReport& report, const Actions& actions,
            const StopFacts& facts, RunRecord& record, PictureShelf* shelf);

    private:
        /// The frame a writer reads, what put it together, and where it says its answer. None of the three is a
        /// member: the renderer and the reconstruction arrive per frame (`Session` is built before the engine, so
        /// there is no renderer to hold), and the record belongs to the run. Bundled to keep them off every
        /// writer's signature.
        struct Writing
        {
            const MWRender::FrameContext& mContext;
            const MWRender::FrameReport& mReport;
            RunRecord& mRecord;
            PictureShelf* mShelf;
        };

        /// Writes a picture, top row first, and keeps it on the shelf where there is one. Says why
        /// it could not in the record, and fails the run, where it returns false.
        static bool writePicture(const Writing& into, const std::filesystem::path& file, std::uint32_t width,
            std::uint32_t height, std::span<const std::uint8_t> pixels);
        static bool writePicture(const Writing& into, const std::filesystem::path& file, std::uint32_t width,
            std::uint32_t height, std::span<const std::uint16_t> samples);

        /// The last measured frame, as a PNG at the capture's depth.
        void writeCapture(const Writing& into, const Actions::Capture& capture);

        /// The last measured frame, added to `mean`, which is written once it holds its count.
        void addToMean(const Writing& into, const Actions::Mean& mean);

        /// Every measured frame's figures, a frame a line.
        void writeFrameTimes(const Writing& into, const std::filesystem::path& file, const FrameSamples& samples);

        /// Every texture the scene holds, vanilla beside de-lit, as one sheet.
        void writeSheet(const Writing& into, const std::filesystem::path& sheet);

        /// One local-map tile of wherever the stop stands.
        void writeMapTile(const Writing& into, const std::filesystem::path& file);

        /// The inventory doll of one person.
        void writeDoll(const Writing& into, const std::string& who, const std::filesystem::path& file);

        /// Lists the textures whose path holds `needle`, and where the meshes wearing them stand.
        void reportFound(const Writing& into, const std::string& needle);

        /// Draws `view` and writes what it drew, right way up.
        ///
        /// **A picture inside the interface is written bottom row first**, which is what
        /// `OffscreenView::getTexture` promises and what the widgets showing one invert V for. A
        /// file wants the other order, so the rows are turned over on the way out.
        void writeView(const Writing& into, MWRender::OffscreenView& view, const std::filesystem::path& file);

        /// Writes a picture that is already in main memory, bottom row first, as a PNG.
        void writeImage(const Writing& into, const osg::Image& drawn, const std::filesystem::path& file);

        /// What a read back lands in, refilled per stop and never freed.
        std::vector<std::uint8_t> mPixels;

        /// The mean `addToMean` is building, and where it goes: one at a time, since the stops one
        /// mean takes run one after another.
        PictureMean mMean;
        std::filesystem::path mMeanFile;

        /// What a sixteen-bit picture is written from — a finished mean, or a summed frame's own —
        /// refilled per picture and never freed.
        std::vector<std::uint16_t> mSamples;
    };
}
