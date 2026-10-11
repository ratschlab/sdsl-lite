#include <cstdint>

#include <sdsl/uint256_t.hpp>

#include <gtest/gtest.h>

namespace
{

sdsl::uint256_t single_bit(int bit)
{
    if (bit < 64)
        return sdsl::uint256_t(uint64_t{1} << bit, 0, 0);
    if (bit < 128)
        return sdsl::uint256_t(0, uint64_t{1} << (bit - 64), 0);
    return sdsl::uint256_t(0, 0, sdsl::uint128_t{1} << (bit - 128));
}

TEST(uint256_t_test, shifts)
{
    for (int bit = 0; bit < 256; ++bit)
    {
        auto const value = single_bit(bit);
        for (int shift = 0; shift < 256; ++shift)
        {
            auto const left = bit + shift < 256 ? single_bit(bit + shift) : sdsl::uint256_t(0);
            auto const right = bit >= shift ? single_bit(bit - shift) : sdsl::uint256_t(0);
            ASSERT_EQ(left, value << shift) << "bit=" << bit << ", shift=" << shift;
            ASSERT_EQ(right, value >> shift) << "bit=" << bit << ", shift=" << shift;
        }
    }
}

TEST(uint256_t_test, lab_integer_operations)
{
    static_assert(!std::is_trivially_copyable_v<sdsl::uint128_t> || std::is_trivially_copyable_v<sdsl::uint256_t>);
    auto const low = (sdsl::uint128_t{1} << 100) | 7;
    auto const high = sdsl::uint128_t{1} << 73;
    sdsl::uint256_t const value(low, high);
    EXPECT_EQ(low, static_cast<sdsl::uint128_t>(value));
    EXPECT_EQ(high, static_cast<sdsl::uint128_t>(value >> 128));
    EXPECT_TRUE(static_cast<bool>(single_bit(200)));
    EXPECT_FALSE(static_cast<bool>(sdsl::uint256_t{}));
    EXPECT_EQ(sdsl::uint256_t(low), value & low);
    EXPECT_EQ(sdsl::uint256_t(low, high), single_bit(201) | low);
    EXPECT_EQ(sdsl::uint256_t{}, value & ~value);
    sdsl::uint256_t assigned;
    assigned = low;
    EXPECT_EQ(sdsl::uint256_t(low), assigned);
    EXPECT_EQ(sdsl::uint256_t(8), (value & uint64_t{7}) + 1);
    EXPECT_GT(value, low);
    EXPECT_TRUE(0ULL != single_bit(200));
    EXPECT_TRUE(0ULL < single_bit(200));
    EXPECT_FALSE(0ULL == single_bit(200));
    EXPECT_FALSE(0ULL >= single_bit(200));
}

} // namespace
