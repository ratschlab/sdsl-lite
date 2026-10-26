#include <algorithm>
#include <array>
#include <utility>

#include <sdsl/enc_vector.hpp>
#include <sdsl/int_vector.hpp>
#include <sdsl/int_vector_mapper.hpp>
#include <sdsl/qsufsort.hpp>
#include <sdsl/util.hpp>

#include <gtest/gtest.h>

namespace
{
template <class Vector>
concept compressible = requires (Vector & v) { sdsl::util::bit_compress(v); };

template <class Vector>
concept expandable = requires (Vector & v) { sdsl::util::expand_width(v, 16); };

template <uint8_t Width>
constexpr bool width_operations_match()
{
    using vector = sdsl::int_vector<Width>;
    using mapper = sdsl::int_vector_mapper<Width>;
    return compressible<vector> == (Width == 0) && expandable<vector> == (Width == 0)
        && compressible<mapper> == (Width == 0) && expandable<mapper> == (Width == 0);
}

static_assert(
    []<size_t... Widths>(std::index_sequence<Widths...>)
    {
        return (width_operations_match<Widths>() && ...);
    }(std::make_index_sequence<65>{}));

TEST(int_vector_width, dynamic_reinterpretation)
{
    sdsl::int_vector<> v(2, 0xF0, 8);
    v.width(4);
    ASSERT_EQ(4, v.size());
    EXPECT_EQ(0, v[0]);
    EXPECT_EQ(15, v[1]);
    EXPECT_EQ(0, v[2]);
    EXPECT_EQ(15, v[3]);
}

TEST(int_vector_width, repacking_preserves_values)
{
    sdsl::int_vector<> v(137, 0, 64);
    for (size_t i = 0; i < v.size(); ++i)
        v[i] = i % 8;
    sdsl::util::bit_compress(v);
    EXPECT_EQ(3, v.width());
    for (uint8_t width : {7, 13, 31, 63, 64})
    {
        sdsl::util::expand_width(v, width);
        EXPECT_EQ(width, v.width());
        ASSERT_EQ(137, v.size());
        for (size_t i = 0; i < v.size(); ++i)
            EXPECT_EQ(i % 8, v[i]);
    }
}

TEST(int_vector_width, encoded_narrow_input)
{
    sdsl::int_vector<8> input{1, 2, 4, 7};
    sdsl::enc_vector<> encoded(input);
    ASSERT_EQ(input.size(), encoded.size());
    for (size_t i = 0; i < input.size(); ++i)
        EXPECT_EQ(input[i], encoded[i]);
}

TEST(int_vector_width, empty_repacking)
{
    sdsl::int_vector<> v;
    sdsl::util::bit_compress(v);
    EXPECT_EQ(1, v.width());
    sdsl::util::expand_width(v, 13);
    EXPECT_EQ(13, v.width());
    EXPECT_TRUE(v.empty());
}

template <uint8_t Width>
void check_suffix_sort()
{
    sdsl::int_vector<> text{2, 1, 2, 1, 0};
    sdsl::int_vector<Width> sa;
    std::array<uint64_t, 5> const expected{4, 3, 1, 2, 0};
    sdsl::qsufsort::construct_sa(sa, text);
    EXPECT_TRUE(std::equal(sa.begin(), sa.end(), expected.begin(), expected.end()));

    auto const file = sdsl::ram_file_name("width-test-" + std::to_string(Width));
    sdsl::int_vector<> file_text(text.size(), 0, Width ? Width : 8);
    std::copy(text.begin(), text.end(), file_text.begin());
    ASSERT_TRUE(sdsl::store_to_file(file_text, file));
    sdsl::qsufsort::construct_sa(sa, file.c_str(), 0);
    EXPECT_TRUE(std::equal(sa.begin(), sa.end(), expected.begin(), expected.end()));
    sdsl::remove(file);
}

TEST(int_vector_width, suffix_sort_fixed_and_dynamic)
{
    check_suffix_sort<0>();
    check_suffix_sort<8>();
    check_suffix_sort<64>();
}

TEST(int_vector_width, suffix_sort_rejects_insufficient_fixed_width)
{
    sdsl::int_vector<> text{7, 1, 0};
    sdsl::int_vector<3> sa;
    EXPECT_THROW(sdsl::qsufsort::construct_sa(sa, text), std::logic_error);
}
} // namespace
