#include <array>
#include <cstdint>

#include <gtest/gtest.h>

#include <components/rtxvulkan/upscale/upscaler.hpp>

namespace Rtx
{
    namespace
    {
        /// A menu offers DLSS where every fact for it holds, before it was made or once it runs, and
        /// nowhere else.
        TEST(RtxUpscalerTest, aMenuOffersDlssWhereEveryFactForItHolds)
        {
            struct Row
            {
                DlssAvailability mAvailability;
                bool mOffered;
            };
            constexpr std::array rows{
                Row{ DlssAvailability::NotNvidia, false },
                Row{ DlssAvailability::NotInstalled, false },
                Row{ DlssAvailability::Present, true },
                Row{ DlssAvailability::Ready, true },
            };
            for (const Row& row : rows)
                EXPECT_EQ(offersDlss(row.mAvailability), row.mOffered) << static_cast<std::int32_t>(row.mAvailability);
        }
    }
}
