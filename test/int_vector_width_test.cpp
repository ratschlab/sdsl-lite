#include <algorithm>
#include <array>
#include <sstream>
#include <string>
#include <utility>

#include <sdsl/coder_fibonacci.hpp>
#include <sdsl/csa_sampling_strategy.hpp>
#include <sdsl/enc_vector.hpp>
#include <sdsl/int_vector.hpp>
#include <sdsl/int_vector_buffer.hpp>
#include <sdsl/int_vector_io_wrappers.hpp>
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

TEST(int_vector_width, text_loader_requires_sufficient_width)
{
    auto const file = sdsl::ram_file_name("width-text");
    {
        sdsl::osfstream out(file);
        out << "0 7 255";
    }
    sdsl::int_vector<> dynamic;
    sdsl::int_vector<16> wide;
    sdsl::int_vector<7> narrow;
    ASSERT_TRUE(sdsl::load_vector_from_file(dynamic, file, 'd'));
    ASSERT_TRUE(sdsl::load_vector_from_file(wide, file, 'd'));
    EXPECT_EQ(8, dynamic.width());
    EXPECT_EQ(16, wide.width());
    EXPECT_TRUE(std::equal(dynamic.begin(), dynamic.end(), wide.begin(), wide.end()));
    EXPECT_THROW(sdsl::load_vector_from_file(narrow, file, 'd'), std::invalid_argument);
    sdsl::remove(file);
}

TEST(int_vector_width, binary_loader_rejects_truncation)
{
    auto const file = sdsl::ram_file_name("width-binary");
    {
        sdsl::osfstream out(file, std::ios::binary);
        char const bytes[] = {1, 0, 0, 1}; // 1 and 256 as two-byte little-endian integers.
        out.write(bytes, sizeof(bytes));
    }
    sdsl::int_vector<> dynamic;
    sdsl::int_vector<16> wide;
    sdsl::int_vector<8> narrow;
    ASSERT_TRUE(sdsl::load_vector_from_file(wide, file, 2));
    EXPECT_EQ(1, wide[0]);
    EXPECT_EQ(256, wide[1]);
    EXPECT_THROW(sdsl::load_vector_from_file(narrow, file, 2), std::invalid_argument);
    EXPECT_THROW(sdsl::load_vector_from_file(dynamic, file, 2, 8), std::invalid_argument);
    EXPECT_THROW(sdsl::load_vector_from_file(narrow, file, 2, 8), std::invalid_argument);
    {
        sdsl::osfstream out(file, std::ios::binary | std::ios::trunc);
        char const bytes[] = {1, 0, char(255), 0};
        out.write(bytes, sizeof(bytes));
    }
    ASSERT_TRUE(sdsl::load_vector_from_file(narrow, file, 2, 8));
    EXPECT_EQ(1, narrow[0]);
    EXPECT_EQ(255, narrow[1]);
    sdsl::remove(file);
}

TEST(int_vector_width, sample_storage_requires_sufficient_width)
{
    sdsl::int_vector<> input{uint64_t{1} << 32};
    using narrow_samples = sdsl::enc_vector<sdsl::coder::elias_delta<>, 128, 32>;
    using wide_samples = sdsl::enc_vector<sdsl::coder::elias_delta<>, 128, 64>;
    EXPECT_THROW((narrow_samples(input)), std::invalid_argument);
    wide_samples encoded(input);
    EXPECT_EQ(input[0], encoded[0]);
}

struct sampling_csa
{
    static constexpr unsigned sa_sample_dens = 2;
};

TEST(int_vector_width, suffix_samples_require_sufficient_width)
{
    sdsl::cache_config config(false, "@", "width-samples");
    auto const file = sdsl::cache_file_name(sdsl::conf::KEY_SA, config);
    sdsl::int_vector<> sa(17, 0, 5);
    sdsl::util::set_to_id(sa);
    ASSERT_TRUE(sdsl::store_to_file(sa, file));
    using narrow_samples = sdsl::_sa_order_sampling<sampling_csa, 4>;
    using wide_samples = sdsl::_sa_order_sampling<sampling_csa, 8>;
    EXPECT_THROW((narrow_samples(config)), std::invalid_argument);
    wide_samples sampled(config);
    for (size_t i = 0; i < sa.size(); i += sampling_csa::sa_sample_dens)
        EXPECT_EQ(i, sampled[i]);
    sdsl::remove(file);
}

TEST(int_vector_width, fibonacci_preserves_exact_width_for_zero)
{
    sdsl::int_vector<8> input{0, 1, 255}, restored;
    sdsl::int_vector<> encoded;
    ASSERT_TRUE(sdsl::coder::fibonacci<>::encode(input, encoded));
    ASSERT_TRUE(sdsl::coder::fibonacci<>::decode(encoded, restored));
    EXPECT_TRUE(std::equal(input.begin(), input.end(), restored.begin(), restored.end()));

    sdsl::int_vector<16> wrong_width;
    EXPECT_THROW(sdsl::coder::fibonacci<>::encode(input, wrong_width), std::invalid_argument);
    EXPECT_THROW(sdsl::coder::fibonacci<>::decode(encoded, wrong_width), std::invalid_argument);
}

TEST(int_vector_width, serialized_layout_requires_exact_width)
{
    sdsl::int_vector<> input(4, 0, 8);
    input[0] = 1;
    input[1] = 127;
    input[2] = 128;
    input[3] = 255;
    auto const file = sdsl::ram_file_name("width-layout");
    ASSERT_TRUE(sdsl::store_to_file(input, file));
    sdsl::int_vector_buffer<8> exact(file);
    EXPECT_EQ(input.size(), exact.size());
    EXPECT_THROW((sdsl::int_vector_buffer<16>(file)), std::invalid_argument);
    exact.close();
    sdsl::remove(file);

    std::stringstream stream;
    sdsl::int_vector_serialize_vbyte_wrapper<> writer(input);
    writer.serialize(stream);
    sdsl::int_vector<8> loaded;
    sdsl::int_vector_load_vbyte_wrapper<8> reader(loaded);
    reader.load(stream);
    EXPECT_TRUE(std::equal(input.begin(), input.end(), loaded.begin(), loaded.end()));
    stream.clear();
    stream.seekg(0);
    sdsl::int_vector<16> wrong;
    sdsl::int_vector_load_vbyte_wrapper<16> wrong_reader(wrong);
    EXPECT_THROW(wrong_reader.load(stream), std::invalid_argument);
}
} // namespace
