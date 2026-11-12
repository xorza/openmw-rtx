#include <array>

#include <gtest/gtest.h>

#include <components/rtx/frame/upscale.hpp>
#include <components/rtxvulkan/upscale/upscaler.hpp>

namespace Rtx
{
    namespace
    {
        /// `auto` is a policy over what the machine says: DLSS only where every fact holds and nothing
        /// refused it, which is also where a menu offers it. A concrete choice stands whatever they say, so an explicit
        /// DLSS fails by name where it is made, and is not turned into FSR here.
        TEST(RtxUpscalerTest, autoChoosesDlssOnlyWhereEveryFactForItHolds)
        {
            struct Row
            {
                DlssAvailability mAvailability;
                UpscalerKind mAuto;
                bool mOffered;
            };
            constexpr std::array rows{
                Row{ DlssAvailability::NotNvidia, UpscalerKind::Fsr, false },
                Row{ DlssAvailability::NotInstalled, UpscalerKind::Fsr, false },
                Row{ DlssAvailability::Present, UpscalerKind::Dlss, true },
                Row{ DlssAvailability::Refused, UpscalerKind::Fsr, false },
                Row{ DlssAvailability::Ready, UpscalerKind::Dlss, true },
            };
            for (const Row& row : rows)
            {
                const int at = static_cast<int>(row.mAvailability);
                EXPECT_EQ(chooseUpscaler(UpscalerChoice::Auto, row.mAvailability), row.mAuto) << at;
                EXPECT_EQ(chooseUpscaler(UpscalerChoice::Fsr, row.mAvailability), UpscalerKind::Fsr) << at;
                EXPECT_EQ(chooseUpscaler(UpscalerChoice::Dlss, row.mAvailability), UpscalerKind::Dlss) << at;
                EXPECT_EQ(offersDlss(row.mAvailability), row.mOffered) << at;
            }
        }

        /// **The upscaler's line says what runs, what was asked, and why `auto` passed DLSS over**: each
        /// fact that stops DLSS names itself, a refusal carries what NGX said, and a concrete choice or
        /// an `auto` that came to DLSS says nothing more.
        TEST(RtxUpscalerTest, theChoicesLineSaysWhatWasAskedAndWhyAutoPassedDlssOver)
        {
            EXPECT_EQ(describeUpscalerChoice("FSR", UpscalerChoice::Auto, DlssAvailability::NotNvidia, ""),
                "FSR (asked auto, DLSS passed over: the device is not NVIDIA's)");
            EXPECT_EQ(describeUpscalerChoice("FSR", UpscalerChoice::Auto, DlssAvailability::NotInstalled, ""),
                "FSR (asked auto, DLSS passed over: its library is not installed)");
            EXPECT_EQ(describeUpscalerChoice("FSR", UpscalerChoice::Auto, DlssAvailability::Refused, "out of memory"),
                "FSR (asked auto, DLSS passed over: it could not start, out of memory)");
            EXPECT_EQ(
                describeUpscalerChoice("DLSS", UpscalerChoice::Auto, DlssAvailability::Ready, ""), "DLSS (asked auto)");
            EXPECT_EQ(
                describeUpscalerChoice("DLSS", UpscalerChoice::Dlss, DlssAvailability::Ready, ""), "DLSS (asked dlss)");
            EXPECT_EQ(
                describeUpscalerChoice("FSR", UpscalerChoice::Fsr, DlssAvailability::NotNvidia, ""), "FSR (asked fsr)");
        }
    }
}
