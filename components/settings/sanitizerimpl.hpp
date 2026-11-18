#pragma once

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <string>
#include <string_view>

#include <osg/Vec3f>

#include "sanitizer.hpp"

namespace Settings
{
    std::unique_ptr<Sanitizer<float>> makeMaxSanitizerFloat(float max);

    std::unique_ptr<Sanitizer<std::int32_t>> makeMaxSanitizerInt(std::int32_t max);

    std::unique_ptr<Sanitizer<std::size_t>> makeMaxSanitizerSize(std::size_t max);

    std::unique_ptr<Sanitizer<std::uint64_t>> makeMaxSanitizerUInt64(std::uint64_t max);

    std::unique_ptr<Sanitizer<float>> makeMaxStrictSanitizerFloat(float max);

    std::unique_ptr<Sanitizer<osg::Vec3f>> makeMaxStrictSanitizerVec3f(const osg::Vec3f& max);

    std::unique_ptr<Sanitizer<float>> makeClampSanitizerFloat(float min, float max);

    std::unique_ptr<Sanitizer<double>> makeClampSanitizerDouble(double min, double max);

    std::unique_ptr<Sanitizer<std::int32_t>> makeClampSanitizerInt(std::int32_t min, std::int32_t max);

    std::unique_ptr<Sanitizer<std::size_t>> makeClampSanitizerSize(std::size_t min, std::size_t max);

    std::unique_ptr<Sanitizer<float>> makeClampStrictMaxSanitizerFloat(float min, float max);

    std::unique_ptr<Sanitizer<std::int32_t>> makeEnumSanitizerInt(std::initializer_list<std::int32_t> values);

    std::unique_ptr<Sanitizer<std::string>> makeEnumSanitizerString(std::initializer_list<std::string> values);

    std::unique_ptr<Sanitizer<float>> makeEqualOrMaxSanitizerFloat(float equal, float max);
}
