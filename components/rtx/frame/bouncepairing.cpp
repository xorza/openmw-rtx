#include "bouncepairing.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <random>

#include <components/rtx/shaders/bouncepairing.h>

namespace Rtx
{
    namespace
    {
        /// The 24 orders of a 2×2 block's four cells.
        constexpr std::array<std::array<std::uint8_t, 4>, 24> sOrders{ {
            { 0, 1, 2, 3 },
            { 0, 1, 3, 2 },
            { 0, 2, 1, 3 },
            { 0, 2, 3, 1 },
            { 0, 3, 1, 2 },
            { 0, 3, 2, 1 },
            { 1, 0, 2, 3 },
            { 1, 0, 3, 2 },
            { 1, 2, 0, 3 },
            { 1, 2, 3, 0 },
            { 1, 3, 0, 2 },
            { 1, 3, 2, 0 },
            { 2, 0, 1, 3 },
            { 2, 0, 3, 1 },
            { 2, 1, 0, 3 },
            { 2, 1, 3, 0 },
            { 2, 3, 0, 1 },
            { 2, 3, 1, 0 },
            { 3, 0, 1, 2 },
            { 3, 0, 2, 1 },
            { 3, 1, 0, 2 },
            { 3, 1, 2, 0 },
            { 3, 2, 0, 1 },
            { 3, 2, 1, 0 },
        } };

        /// `value` wrapped into `[-size / 2, size / 2)`: the step to the nearer copy on the torus.
        std::int32_t wrapped(std::int32_t value, std::int32_t size)
        {
            const std::int32_t half = size / 2;
            return ((value + half) % size + size) % size - half;
        }

        /// Where each link's two cells stand, filled from the grid.
        struct Ends
        {
            std::vector<std::uint32_t> mFirst;
            std::vector<std::uint32_t> mSecond;
        };

        void findEnds(const std::vector<std::uint32_t>& grid, Ends& ends)
        {
            constexpr std::uint32_t none = ~0u;
            std::fill(ends.mFirst.begin(), ends.mFirst.end(), none);
            for (std::uint32_t cell = 0; cell < grid.size(); ++cell)
            {
                std::uint32_t& first = ends.mFirst[grid[cell]];
                if (first == none)
                    first = cell;
                else
                    ends.mSecond[grid[cell]] = cell;
            }
        }

        /// The steps' deviation along each axis: the root of the mean squared step, over both axes.
        float deviationOf(const Ends& ends, std::uint32_t size)
        {
            const auto side = static_cast<std::int32_t>(size);
            double squares = 0.0;
            for (std::size_t link = 0; link < ends.mFirst.size(); ++link)
            {
                const auto from = static_cast<std::int32_t>(ends.mFirst[link]);
                const auto to = static_cast<std::int32_t>(ends.mSecond[link]);
                const std::int32_t across = wrapped(to % side - from % side, side);
                const std::int32_t down = wrapped(to / side - from / side, side);
                squares += static_cast<double>(across * across + down * down);
            }
            return static_cast<float>(std::sqrt(squares / (2.0 * static_cast<double>(ends.mFirst.size()))));
        }
    }

    BouncePairing::BouncePairing(const std::uint32_t size, const float deviation, const std::uint32_t seed)
        : mSize(size)
    {
        assert(size >= 2 && size % 2 == 0 && "a pairing whose blocks do not tile it");
        assert(deviation >= 0.8f && deviation <= static_cast<float>(size) / 6.0f
            && "a pairing deviation no shuffle reaches");

        const std::uint32_t cells = size * size;
        std::vector<std::uint32_t> grid(cells);
        for (std::uint32_t cell = 0; cell < cells; ++cell)
            grid[cell] = cell / 2;

        Ends ends{ .mFirst = std::vector<std::uint32_t>(cells / 2), .mSecond = std::vector<std::uint32_t>(cells / 2) };

        // **Measured only past where the deviation could stand**: one shuffle adds about two pixels
        // squared to its square (the paper's fit, `n ≈ σ² / 2`), so the first nine tenths of that
        // run without asking, and the rest asks after every shuffle.
        const auto unasked = static_cast<std::uint32_t>(0.9f * deviation * deviation / 2.0f);

        // `mt19937`'s words are the standard's to the bit, and the pick is a word times the count
        // shifted down, as `BlueNoise` draws: one texture on every machine.
        std::mt19937 words(seed);
        const std::uint32_t half = size / 2;
        for (std::uint32_t shuffle = 0;; ++shuffle)
        {
            const std::uint32_t moved = shuffle % 2;
            for (std::uint32_t blockDown = 0; blockDown < half; ++blockDown)
                for (std::uint32_t blockAcross = 0; blockAcross < half; ++blockAcross)
                {
                    const std::uint32_t left = (2 * blockAcross + moved) % size;
                    const std::uint32_t top = (2 * blockDown + moved) % size;
                    const std::uint32_t right = (left + 1) % size;
                    const std::uint32_t bottom = (top + 1) % size;
                    const std::array<std::uint32_t, 4> at{ top * size + left, top * size + right, bottom * size + left,
                        bottom * size + right };
                    const std::array<std::uint32_t, 4> held{ grid[at[0]], grid[at[1]], grid[at[2]], grid[at[3]] };
                    const auto& order = sOrders[static_cast<std::size_t>((std::uint64_t{ words() } * 24) >> 32)];
                    for (std::size_t i = 0; i < 4; ++i)
                        grid[at[i]] = held[order[i]];
                }

            if (shuffle + 1 < unasked)
                continue;

            findEnds(grid, ends);
            mDeviation = deviationOf(ends, size);
            if (mDeviation >= deviation)
                break;
            assert(shuffle < 64 * cells && "a pairing that stopped spreading");
        }

        const auto side = static_cast<std::int32_t>(size);
        const auto pack = [](std::int32_t across, std::int32_t down) {
            return (static_cast<std::uint32_t>(across) & 0xFFFFu) | (static_cast<std::uint32_t>(down) << 16);
        };
        mSteps.resize(cells);
        for (std::size_t link = 0; link < ends.mFirst.size(); ++link)
        {
            const auto from = static_cast<std::int32_t>(ends.mFirst[link]);
            const auto to = static_cast<std::int32_t>(ends.mSecond[link]);
            const std::int32_t across = wrapped(to % side - from % side, side);
            const std::int32_t down = wrapped(to / side - from / side, side);
            mSteps[static_cast<std::size_t>(from)] = pack(across, down);
            mSteps[static_cast<std::size_t>(to)] = pack(-across, -down);
        }
    }

    PairingStep BouncePairing::stepAt(const std::uint32_t across, const std::uint32_t down) const
    {
        assert(across < mSize && down < mSize && "a pairing texel outside the texture");
        const std::uint32_t word = mSteps[std::size_t{ down } * mSize + across];
        return PairingStep{ .mAcross = static_cast<std::int16_t>(word & 0xFFFFu),
            .mDown = static_cast<std::int16_t>(word >> 16) };
    }

    std::vector<std::uint32_t> bouncePairingSteps(const std::uint32_t height)
    {
        const float radius
            = std::max(Shaders::BOUNCE_RADIUS_SHARE * static_cast<float>(height), Shaders::BOUNCE_RADIUS_LEAST);
        const float deviation = std::clamp(std::sqrt(8.0f / (9.0f * std::numbers::pi_v<float>)) * radius, 0.8f,
            static_cast<float>(Shaders::BOUNCE_PAIRING_SIZE_1) / 6.0f);

        const BouncePairing first(Shaders::BOUNCE_PAIRING_SIZE_0, deviation, 0);
        const BouncePairing second(Shaders::BOUNCE_PAIRING_SIZE_1, deviation, 1);
        std::vector<std::uint32_t> steps(first.getSteps().begin(), first.getSteps().end());
        steps.insert(steps.end(), second.getSteps().begin(), second.getSteps().end());
        return steps;
    }
}
