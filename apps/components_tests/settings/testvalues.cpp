#include <cstdint>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <components/misc/strings/conversion.hpp>
#include <components/rtx/common/error.hpp>
#include <components/rtx/frame/upscale.hpp>
#include <components/settings/parser.hpp>
#include <components/settings/values.hpp>

namespace Settings
{
    namespace
    {
        using namespace testing;

        struct SettingsValuesTest : Test
        {
            const std::filesystem::path mSettingsDefaultPath = std::filesystem::path{ OPENMW_PROJECT_SOURCE_DIR }
                / "files" / Misc::StringUtils::stringToU8String("settings-default.cfg");

            SettingsValuesTest()
            {
                Manager::mDefaultSettings.clear();
                Manager::mUserSettings.clear();
                Manager::mChangedSettings.clear();
                SettingsFileParser parser;
                parser.loadSettingsFile(mSettingsDefaultPath, Manager::mDefaultSettings);
            }
        };

        TEST_F(SettingsValuesTest, shouldLoadFromSettingsManager)
        {
            Index index;
            Values values(index);
            EXPECT_EQ(values.mCamera.mFieldOfView.get(), 60);
        }

        TEST_F(SettingsValuesTest, shouldFillIndexOnLoad)
        {
            Index index;
            Values values(index);
            EXPECT_EQ(index.get<float>("Camera", "field of view").get(), 60);
        }

        TEST_F(SettingsValuesTest, constructorShouldThrowExceptionOnMissingSetting)
        {
            Manager::mDefaultSettings.erase({ "Camera", "field of view" });
            Index index;
            EXPECT_THROW([&] { Values values(index); }(), std::runtime_error);
        }

        TEST_F(SettingsValuesTest, constructorShouldSanitize)
        {
            Manager::mUserSettings[std::make_pair("Camera", "field of view")] = "-1";
            Index index;
            Values values(index);
            EXPECT_EQ(values.mCamera.mFieldOfView.get(), 1);
        }

        TEST_F(SettingsValuesTest, constructorShouldThrowOnNumberThatIsNotFinite)
        {
            for (const char* spelled : { "inf", "-inf", "nan", "infinity" })
            {
                Manager::mUserSettings[std::make_pair("Video", "gamma")] = spelled;
                Index index;
                EXPECT_THROW([&] { Values values(index); }(), std::runtime_error) << spelled;
            }
        }

        TEST_F(SettingsValuesTest, constructorShouldThrowOnVectorComponentThatIsNotFinite)
        {
            Manager::mUserSettings[std::make_pair("Fog", "sky rtt resolution")] = "512 nan";
            Index index;
            EXPECT_THROW([&] { Values values(index); }(), std::runtime_error);
        }

        /// **An upscale mode and an upscaler are read by their spelling lists at load**, as the
        /// documentation spells them, and written back by the same name. A mode the list does not
        /// hold is refused: a typo traced under another mode would be a session of the wrong picture,
        /// and `off` is no mode but the upscaler's own choice. **An upscaler the list does not hold is
        /// FSR** (`sUpscalerUnread`), the shipped default: a file an older build wrote holds `auto`.
        TEST_F(SettingsValuesTest, constructorShouldReadAModeByItsSpellingAndRefuseOneItLacks)
        {
            for (const auto& [mode, spelling] : Rtx::sUpscaleModeNames.mNames)
            {
                Manager::mUserSettings[std::make_pair("RTX", "upscale")] = std::string(spelling);
                Index index;
                Values values(index);
                EXPECT_EQ(values.mRTX.mUpscale.get(), mode) << spelling;
            }
            for (const auto& [upscaler, spelling] : Rtx::sUpscalerNames.mNames)
            {
                Manager::mUserSettings[std::make_pair("RTX", "upscaler")] = std::string(spelling);
                Index index;
                Values values(index);
                EXPECT_EQ(values.mRTX.mUpscaler.get(), upscaler) << spelling;
            }

            {
                Index index;
                Values values(index);
                values.mRTX.mUpscale.set(Rtx::Upscale::UltraPerformance);
                EXPECT_EQ(Manager::mUserSettings.at({ "RTX", "upscale" }), "ultraperformance");
                values.mRTX.mUpscaler.set(Rtx::UpscalerChoice::Dlss);
                EXPECT_EQ(Manager::mUserSettings.at({ "RTX", "upscaler" }), "dlss");
            }

            EXPECT_EQ(
                Manager::mDefaultSettings.at({ "RTX", "upscaler" }), Rtx::sUpscalerNames.name(Rtx::sUpscalerUnread))
                << "what an unread upscaler is read as is the shipped default";
            for (const char* const unread : { "auto", "Fsr", "" })
            {
                Manager::mUserSettings[std::make_pair("RTX", "upscaler")] = unread;
                Index index;
                Values values(index);
                EXPECT_EQ(values.mRTX.mUpscaler.get(), Rtx::UpscalerChoice::Fsr) << unread;
            }

            for (const char* const refused : { "Quality", "off" })
            {
                Manager::mUserSettings[std::make_pair("RTX", "upscale")] = refused;
                Index index;
                EXPECT_THROW([&] { Values values(index); }(), Rtx::InputError) << refused;
            }
        }

        TEST_F(SettingsValuesTest, constructorWithDefaultShouldDoLookup)
        {
            Manager::mUserSettings[std::make_pair("category", "value")] = "13";
            Index index;
            SettingValue<std::int32_t> value{ index, "category", "value", 42 };
            EXPECT_EQ(value.get(), 13);
            value.reset();
            EXPECT_EQ(value.get(), 42);
        }

        TEST_F(SettingsValuesTest, constructorWithDefaultShouldSanitize)
        {
            Manager::mUserSettings[std::make_pair("category", "value")] = "2";
            Index index;
            SettingValue<std::int32_t> value{ index, "category", "value", -1, Settings::makeClampSanitizerInt(0, 1) };
            EXPECT_EQ(value.get(), 1);
            value.reset();
            EXPECT_EQ(value.get(), 0);
        }

        TEST_F(SettingsValuesTest, constructorWithDefaultShouldFallbackToDefault)
        {
            Index index;
            const SettingValue<std::int32_t> value{ index, "category", "value", 42 };
            EXPECT_EQ(value.get(), 42);
        }

        TEST_F(SettingsValuesTest, moveConstructorShouldSetDefaults)
        {
            Index index;
            Values defaultValues(index);
            Manager::mUserSettings.emplace(std::make_pair("Camera", "field of view"), "61");
            Values values(std::move(defaultValues));
            EXPECT_EQ(values.mCamera.mFieldOfView.get(), 61);
            values.mCamera.mFieldOfView.reset();
            EXPECT_EQ(values.mCamera.mFieldOfView.get(), 60);
        }

        TEST_F(SettingsValuesTest, moveConstructorShouldSanitize)
        {
            Index index;
            Values defaultValues(index);
            Manager::mUserSettings[std::make_pair("Camera", "field of view")] = "-1";
            Values values(std::move(defaultValues));
            EXPECT_EQ(values.mCamera.mFieldOfView.get(), 1);
        }

        TEST_F(SettingsValuesTest, moveConstructorShouldThrowOnMissingSetting)
        {
            Index index;
            SettingValue<std::int32_t> defaultValue{ index, "category", "value", 42 };
            EXPECT_THROW([&] { SettingValue<std::int32_t> value(std::move(defaultValue)); }(), std::runtime_error);
        }

        TEST_F(SettingsValuesTest, findShouldThrowExceptionOnTypeMismatch)
        {
            Index index;
            Values values(index);
            EXPECT_THROW(index.find<std::int32_t>("Camera", "field of view"), std::invalid_argument);
        }

        TEST_F(SettingsValuesTest, findShouldReturnNullptrForAbsentSetting)
        {
            Index index;
            Values values(index);
            EXPECT_EQ(index.find<std::int32_t>("foo", "bar"), nullptr);
        }

        TEST_F(SettingsValuesTest, getShouldThrowExceptionForAbsentSetting)
        {
            Index index;
            Values values(index);
            EXPECT_THROW(index.get<std::int32_t>("foo", "bar").get(), std::invalid_argument);
        }

        TEST_F(SettingsValuesTest, setShouldChangeManagerUserSettings)
        {
            Index index;
            Values values(index);
            values.mCamera.mFieldOfView.set(42);
            EXPECT_EQ(Manager::mUserSettings.at({ "Camera", "field of view" }), "42");
            EXPECT_THAT(Manager::mChangedSettings, ElementsAre(std::make_pair("Camera", "field of view")));
        }

        TEST_F(SettingsValuesTest, setShouldNotChangeManagerChangedSettingsForNoChange)
        {
            Index index;
            Values values(index);
            values.mCamera.mFieldOfView.set(values.mCamera.mFieldOfView.get());
            EXPECT_THAT(Manager::mChangedSettings, ElementsAre());
        }
    }
}
