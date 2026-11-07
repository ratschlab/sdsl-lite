#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <sdsl/int_vector.hpp>
#include <sdsl/int_vector_buffer.hpp>
#include <sdsl/int_vector_mapper.hpp>
#include <sdsl/sd_vector.hpp>
#include <sdsl/rank_support_v5.hpp>
#include <sdsl/select_support_mcl.hpp>
#include <sdsl/select_support_scan.hpp>

#include <gtest/gtest.h>

namespace
{

class lab_storage_test : public ::testing::Test
{
protected:
    std::string file = "lab_storage_" + std::to_string(sdsl::util::pid()) + ".sdsl";
    void TearDown() override
    {
        std::remove(file.c_str());
    }
};

TEST_F(lab_storage_test, legacy_vector_headers)
{
    sdsl::int_vector<> dynamic(5, 3, 13);
    std::ostringstream out;
    dynamic.serialize(out);
    auto const data = out.str();
    uint64_t bits = 65;
    EXPECT_EQ(std::string(reinterpret_cast<char const *>(&bits), sizeof(bits)), data.substr(0, 8));
    EXPECT_EQ(13, static_cast<unsigned char>(data[8]));
    EXPECT_EQ(25u, data.size()); // 8 size bytes, 1 width byte, 2 words.

    sdsl::bit_vector fixed(65, 0);
    out.str("");
    fixed.serialize(out);
    EXPECT_EQ(24u, out.str().size()); // Fixed-width vectors omit the width byte.
}

TEST_F(lab_storage_test, mapped_ownership_and_resize)
{
    sdsl::int_vector<> const original(5, 3, 13);
    ASSERT_TRUE(sdsl::store_to_file(original, file));
    sdsl::int_vector<> mapped;
    {
        sdsl::mmap_ifstream in(file);
        mapped.load(in);
    }
    EXPECT_EQ(original, mapped); // Mapping outlives the stream.
    sdsl::int_vector<> moved(std::move(mapped));
    sdsl::int_vector<> swapped(2, 1, 13);
    swapped.swap(moved);
    EXPECT_EQ(original, swapped);
    swapped.push_back(7); // Detach even when the mapped capacity has spare bits.
    EXPECT_EQ(7u, swapped[5]);
    for (size_t i = 0; i < original.size(); ++i)
        EXPECT_EQ(original[i], swapped[i]);
    swapped.resize(65); // Retain v3's extra padding word on owned storage.
    EXPECT_EQ(7u, swapped[5]);
    sdsl::int_vector<> disk;
    std::ifstream in(file);
    disk.load(in);
    EXPECT_EQ(original, disk);
}

TEST_F(lab_storage_test, mapper_at_file_offset)
{
    uint64_t const prefix = 0x0123456789abcdefULL;
    {
        std::ofstream out(file, std::ios::binary);
        out.write(reinterpret_cast<char const *>(&prefix), sizeof(prefix));
        sdsl::int_vector<>(5, 3, 13).serialize(out);
    }
    {
        sdsl::int_vector_mapper<> mapped(file, false, false, sizeof(prefix));
        EXPECT_EQ(5u, mapped.size());
        mapped.resize(20);
        mapped[19] = 91;
        EXPECT_EQ(mapped.bit_size(), mapped.wrapper().bit_size());
        EXPECT_GE(mapped.wrapper().bit_capacity(), mapped.bit_size());
    }
    std::ifstream in(file, std::ios::binary);
    uint64_t got_prefix;
    in.read(reinterpret_cast<char *>(&got_prefix), sizeof(got_prefix));
    EXPECT_EQ(prefix, got_prefix);
    sdsl::int_vector<> got;
    got.load(in);
    ASSERT_EQ(20u, got.size());
    EXPECT_EQ(3u, got[0]);
    EXPECT_EQ(91u, got[19]);
}

TEST_F(lab_storage_test, writable_mapper_preserves_surrounding_bits)
{
    for (uint8_t width : {1, 7, 13, 32, 64})
        for (size_t prefix_size = 0; prefix_size < 8; ++prefix_size)
        {
            std::string const prefix(prefix_size, 'x');
            sdsl::int_vector<> expected(81, 0, width);
            {
                std::ofstream out(file, std::ios::binary);
                out.write(prefix.data(), prefix.size());
                expected.serialize(out);
            }
            {
                sdsl::int_vector_mapper<> mapped(file, false, false, prefix_size);
                for (size_t i = 0; i < mapped.size(); ++i)
                    mapped[i] = expected[i] = i * 17;
                for (size_t i = 1; i < mapped.size(); i += 3)
                {
                    mapped[i] += 5;
                    expected[i] += 5;
                }
                mapped.resize(103);
                expected.resize(103);
                for (size_t i = 81; i < mapped.size(); ++i)
                    mapped[i] = expected[i] = i * 7;
            }
            std::ifstream in(file, std::ios::binary);
            std::string stored_prefix(prefix_size, '\0');
            in.read(stored_prefix.data(), stored_prefix.size());
            EXPECT_EQ(prefix, stored_prefix);
            sdsl::int_vector<> stored;
            stored.load(in);
            EXPECT_EQ(expected, stored);
        }
}

TEST_F(lab_storage_test, buffer_iterator_preserves_v3_operations)
{
    using buffer_type = sdsl::int_vector_buffer<>;
    buffer_type buffer(file, std::ios::out, 8, 13);
    for (uint64_t i = 0; i < 100; ++i)
        buffer.push_back(i * 2);

    buffer_type::size_type const size = buffer.size();
    auto const first = buffer.begin();
    auto const last = buffer.end();
    auto it = first;
    it = last;
    EXPECT_EQ(size, last - first);
    EXPECT_EQ(-static_cast<buffer_type::difference_type>(size), first - last);
    EXPECT_EQ(last, it);
    EXPECT_LT(first, last);
    EXPECT_LE(first, first);
    EXPECT_GT(last, first);
    EXPECT_GE(last, last);
    auto const found = std::lower_bound(first, last, 91);
    EXPECT_EQ(92u, static_cast<uint64_t>(*found));
    EXPECT_EQ(46, found - first);
}

TEST_F(lab_storage_test, sparse_vector_stream_load)
{
    sdsl::bit_vector bits(129, 0);
    bits[64] = bits[128] = 1;
    sdsl::sd_vector<> const expected(bits);
    ASSERT_TRUE(sdsl::store_to_file(expected, file));
    sdsl::sd_vector<> got;
    std::ifstream in(file);
    got.load(in);
    ASSERT_EQ(expected.size(), got.size());
    for (size_t i = 0; i < got.size(); ++i)
        EXPECT_EQ(expected[i], got[i]);
}


// Embedded vectors need not start at a machine-word boundary.
TEST_F(lab_storage_test, mapped_rank_select_at_every_byte_offset)
{
    for (size_t offset = 0; offset < 8; ++offset)
        for (size_t size : {0, 1, 63, 64, 65, 512, 2048, 32704})
        {
            SCOPED_TRACE(::testing::Message() << offset << ":" << size);
            sdsl::bit_vector bits(size, 0);
            for (size_t i = 0; i < size; ++i)
                bits[i] = (i % 3 == 0);
            sdsl::rank_support_v5<> rank(&bits);
            sdsl::select_support_mcl<> ones(&bits);
            sdsl::select_support_scan<0> zeros(&bits);
            {
                std::ofstream out(file, std::ios::binary);
                out << std::string(offset, 'x');
                bits.serialize(out);
                rank.serialize(out);
                ones.serialize(out);
                zeros.serialize(out);
            }
            sdsl::bit_vector mapped;
            sdsl::rank_support_v5<> loaded_rank;
            sdsl::select_support_mcl<> loaded_ones;
            sdsl::select_support_scan<0> loaded_zeros;
            {
                sdsl::mmap_ifstream in(file);
                in.seekg(offset);
                mapped.load(in);
                loaded_rank.load(in, &mapped);
                loaded_ones.load(in, &mapped);
                loaded_zeros.load(in, &mapped);
            }
            ASSERT_TRUE(mapped.is_mapped());
            auto const & view = mapped;
            size_t count = 0, zero_count = 0;
            for (size_t i = 0; i < size; ++i)
            {
                EXPECT_EQ(bits[i], view[i]);
                EXPECT_EQ(count, loaded_rank(i));
                if (bits[i])
                    EXPECT_EQ(i, loaded_ones(++count));
                else
                    EXPECT_EQ(i, loaded_zeros(++zero_count));
            }
            EXPECT_EQ(count, loaded_rank(size));
            EXPECT_EQ(count, sdsl::util::cnt_one_bits(view));
            sdsl::bit_vector copy(view);
            EXPECT_FALSE(copy.is_mapped());
            EXPECT_EQ(bits, copy);
            if (size)
            {
                copy[0] = !copy[0];
                EXPECT_EQ(bits[0], view[0]);
            }
            sdsl::bit_vector combined(size, 0);
            combined |= view;
            EXPECT_EQ(bits, combined);
        }
}


template <uint8_t pattern>
void check_mapped_pattern(const std::string & file)
{
    for (size_t offset = 0; offset < 8; ++offset)
        for (size_t size : {1, 63, 64, 65, 512})
        {
            sdsl::bit_vector bits(size, 0);
            for (size_t i = 0; i < size; ++i)
                bits[i] = (i % 5 < 2);
            sdsl::rank_support_v5<pattern, 2> rank(&bits);
            sdsl::select_support_mcl<pattern, 2> select(&bits);
            {
                std::ofstream out(file, std::ios::binary);
                out << std::string(offset, 'x');
                bits.serialize(out);
                rank.serialize(out);
                select.serialize(out);
            }
            sdsl::bit_vector mapped;
            sdsl::rank_support_v5<pattern, 2> loaded_rank;
            sdsl::select_support_mcl<pattern, 2> loaded_select;
            {
                sdsl::mmap_ifstream in(file);
                in.seekg(offset);
                mapped.load(in);
                loaded_rank.load(in, &mapped);
                loaded_select.load(in, &mapped);
            }
            for (size_t i = 0; i <= size; ++i)
                EXPECT_EQ(rank(i), loaded_rank(i));
            for (size_t i = 1; i <= rank(size); ++i)
                EXPECT_EQ(select(i), loaded_select(i));
        }
}

TEST_F(lab_storage_test, mapped_two_bit_patterns)
{
    check_mapped_pattern<00>(file);
    check_mapped_pattern<01>(file);
    check_mapped_pattern<10>(file);
    check_mapped_pattern<11>(file);
}

TEST_F(lab_storage_test, rank_at_unpadded_mapped_endpoint)
{
    for (size_t size : {0, 64, 32704})
    {
        sdsl::bit_vector bits(size, 1);
        sdsl::rank_support_v5<> rank(&bits);
        ASSERT_TRUE(sdsl::store_to_file(bits, file));
        sdsl::bit_vector mapped;
        {
            sdsl::mmap_ifstream in(file);
            mapped.load(in);
        }
        rank.set_vector(&mapped);
        EXPECT_EQ(size, rank(size));
        EXPECT_EQ(0u, mapped.get_int(size, 0));
    }
}


TEST_F(lab_storage_test, mapped_dynamic_indexing_at_every_width_and_byte_offset)
{
    for (uint8_t width = 1; width <= 64; ++width)
        for (size_t offset = 0; offset < 8; ++offset)
        {
            SCOPED_TRACE(::testing::Message() << unsigned(width) << ":" << offset);
            sdsl::int_vector<> original(65, 0, width);
            for (size_t i = 0; i < original.size(); ++i)
                original[i] = (i * 0x9e3779b97f4a7c15ULL + 1) & sdsl::bits::lo_set[width];
            {
                std::ofstream out(file, std::ios::binary);
                out << std::string(offset, 'x');
                original.serialize(out);
            }
            sdsl::int_vector<> mapped;
            {
                sdsl::mmap_ifstream in(file);
                in.seekg(offset);
                mapped.load(in);
            }
            auto const & view = mapped;
            ASSERT_EQ(width, view.width());
            for (size_t i = 0; i < original.size(); ++i)
                EXPECT_EQ(original[i], view[i]);
            EXPECT_EQ(0u, view.get_int(0, 0));
            EXPECT_EQ(0u, view.get_int(view.bit_size(), 0));
#ifdef SDSL_DEBUG
            EXPECT_THROW(view.get_int(view.bit_size() + 1, 0), std::out_of_range);
            EXPECT_THROW(view.get_int(view.bit_size(), 1), std::out_of_range);
#endif
        }
    sdsl::int_vector<> const normalized(1, 42, 0);
    EXPECT_EQ(64u, normalized.width());
    EXPECT_EQ(42u, normalized[0]);
}


TEST_F(lab_storage_test, mapped_sparse_lookup_bucket_and_word_boundaries)
{
    for (uint64_t size : {uint64_t{1}, uint64_t{65}, uint64_t{4097}, (uint64_t{1} << 40) + 65})
        for (size_t offset = 0; offset < 8; ++offset)
            for (unsigned pattern = 0; pattern < 3; ++pattern)
            {
                SCOPED_TRACE(::testing::Message() << size << ":" << offset << ":" << pattern);
                std::vector<uint64_t> positions;
                if (pattern == 1)
                    positions = {size - 1};
                else if (pattern == 2)
                {
                    for (uint64_t value : {uint64_t{0}, uint64_t{1}, uint64_t{4}, uint64_t{5},
                                           uint64_t{6}, uint64_t{63}, uint64_t{64}, uint64_t{65},
                                           size / 2, size - 1})
                        if (value < size)
                            positions.push_back(value);
                    std::sort(positions.begin(), positions.end());
                    positions.erase(std::unique(positions.begin(), positions.end()), positions.end());
                }
                sdsl::sd_vector_builder builder(size, positions.size());
                for (auto value : positions)
                    builder.set(value);
                sdsl::sd_vector<> original(builder);
                {
                    std::ofstream out(file, std::ios::binary);
                    out << std::string(offset, 'x');
                    original.serialize(out);
                }
                sdsl::sd_vector<> mapped;
                {
                    sdsl::mmap_ifstream in(file);
                    in.seekg(offset);
                    mapped.load(in);
                }
                std::vector<uint64_t> queries;
                for (uint64_t i = 0; i < std::min(size, uint64_t{4097}); ++i)
                    queries.push_back(i);
                for (auto value : positions)
                    for (uint64_t distance = 0; distance < 65 && distance <= value; ++distance)
                        queries.push_back(value - distance);
                queries.push_back(size - 1);
                for (auto query : queries)
                {
                    bool const expected = std::binary_search(positions.begin(), positions.end(), query);
                    EXPECT_EQ(expected, original[query]);
                    EXPECT_EQ(expected, mapped[query]);
                    auto const length = static_cast<uint8_t>(std::min(uint64_t{64}, size - query));
                    uint64_t word = 0;
                    for (auto value : positions)
                        if (value >= query && value - query < length)
                            word |= uint64_t{1} << (value - query);
                    EXPECT_EQ(word, mapped.get_int(query, length));
                }
            }
}

} // namespace
