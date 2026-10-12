#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

#include <sdsl/int_vector.hpp>
#include <sdsl/int_vector_buffer.hpp>
#include <sdsl/int_vector_mapper.hpp>
#include <sdsl/sd_vector.hpp>

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

} // namespace
