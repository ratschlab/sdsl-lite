// Types with view storage read their payloads from a shared_mapper_ifstream
// without copying them, and behave like the owned types.
#include <cstdio>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include <sdsl/packed_vector_view.hpp>
#include <sdsl/select_support_mcl.hpp>
#include <sdsl/shared_mapper.hpp>

#include <gtest/gtest.h>

namespace
{

using sdsl::packed_vector_view;

class mapped_storage_test : public ::testing::Test
{
protected:
    std::string file = "mapped_storage_" + std::to_string(sdsl::util::pid()) + ".sdsl";
    void TearDown() override
    {
        std::remove(file.c_str());
    }

    // Write `prefix` padding bytes and then the serialization of `value`.
    template <class T>
    std::string write(T const & value, size_t prefix)
    {
        std::ostringstream out;
        out << std::string(prefix, 'x');
        value.serialize(out);
        std::ofstream(file, std::ios::binary) << out.str();
        return out.str().substr(prefix);
    }

    template <class T>
    static std::string serialized(T const & value)
    {
        std::ostringstream out;
        value.serialize(out);
        return out.str();
    }
};

sdsl::bit_vector random_bits(uint64_t size, double density, unsigned seed)
{
    std::mt19937_64 rng(seed);
    std::bernoulli_distribution bit(density);
    sdsl::bit_vector bits(size, 0);
    for (uint64_t i = 0; i < size; ++i)
        bits[i] = bit(rng);
    return bits;
}

TEST_F(mapped_storage_test, view_load_maps_and_copies)
{
    sdsl::int_vector<> values(1000, 0, 13);
    for (size_t i = 0; i < values.size(); ++i)
        values[i] = (i * 7919) & 0x1fff;
    for (size_t prefix = 0; prefix < 9; ++prefix)
    {
        auto const bytes = write(values, prefix);
        packed_vector_view<0> mapped, copied;
        {
            sdsl::shared_mapper_ifstream in(file, std::ios::binary);
            in.seekg(prefix);
            mapped.load(in);
            EXPECT_EQ(prefix + bytes.size(), static_cast<size_t>(in.tellg()));
            EXPECT_EQ(reinterpret_cast<std::byte const *>(in.mapper()->data()) + prefix + 9, mapped.bytes().data());
        }
        {
            std::ifstream in(file, std::ios::binary);
            in.seekg(prefix);
            copied.load(in);
        }
        std::remove(file.c_str()); // The mapping keeps the payload alive.
        ASSERT_EQ(values.size(), mapped.size());
        for (size_t i = 0; i < values.size(); ++i)
        {
            ASSERT_EQ(values[i], mapped[i]);
            ASSERT_EQ(values[i], copied[i]);
        }
        EXPECT_EQ(bytes, serialized(mapped));
        EXPECT_EQ(bytes, serialized(copied));
        EXPECT_EQ(serialized(values), bytes);
    }
}

TEST_F(mapped_storage_test, view_load_rejects_truncated_payload)
{
    sdsl::bit_vector bits(1000, 1);
    auto bytes = write(bits, 0);
    std::ofstream(file, std::ios::binary) << bytes.substr(0, bytes.size() - 8);
    sdsl::shared_mapper_ifstream in(file, std::ios::binary);
    packed_vector_view<1> view;
    EXPECT_THROW(view.load(in), std::ios_base::failure);
}

// Load a bit vector with its select support from a mapped and a plain stream.
template <class Select, class Bits>
void check_select(sdsl::bit_vector const & bits, Bits const & loaded, Select const & select)
{
    uint64_t ones = 0;
    for (uint64_t i = 0; i < bits.size(); ++i)
    {
        if (bits[i] == Select::bit_pat)
        {
            ++ones;
            ASSERT_EQ(i, select.select(ones));
        }
        ASSERT_EQ(bits[i], loaded[i]);
    }
}

template <uint8_t t_b>
void check_mcl_storage(mapped_storage_test & test, std::string const & file, unsigned seed)
{
    using view_select = sdsl::select_support_mcl<t_b, 1, packed_vector_view<1>, packed_vector_view>;
    // Below 100000 bits MCL uses its slow construction, above it the fast one.
    for (uint64_t size : {5000ull, 300000ull})
    {
        for (double density : {0.001, 0.5, 0.999})
        {
            auto const bits = random_bits(size, density, seed++);
            packed_vector_view<1> owned_bits(bits);
            view_select owned(&owned_bits);
            sdsl::select_support_mcl<t_b, 1> reference(&bits);
            std::ostringstream ref_out;
            reference.serialize(ref_out);
            for (size_t prefix : {0, 3, 8})
            {
                {
                    std::ostringstream out;
                    out << std::string(prefix, 'x');
                    owned_bits.serialize(out);
                    owned.serialize(out);
                    std::ofstream(file, std::ios::binary) << out.str();
                }
                packed_vector_view<1> mapped_bits, copied_bits;
                view_select mapped, copied;
                {
                    sdsl::shared_mapper_ifstream in(file, std::ios::binary);
                    in.seekg(prefix);
                    mapped_bits.load(in);
                    mapped.load(in, &mapped_bits);
                    ASSERT_TRUE(in);
                }
                {
                    std::ifstream in(file, std::ios::binary);
                    in.seekg(prefix);
                    copied_bits.load(in);
                    copied.load(in, &copied_bits);
                }
                std::remove(file.c_str());
                check_select(bits, mapped_bits, mapped);
                check_select(bits, copied_bits, copied);
                std::ostringstream a, b;
                mapped.serialize(a);
                copied.serialize(b);
                EXPECT_EQ(ref_out.str(), a.str());
                EXPECT_EQ(ref_out.str(), b.str());
                // Copies and moves keep the mapping alive.
                view_select copy(mapped);
                view_select moved(std::move(mapped));
                mapped = view_select();
                check_select(bits, mapped_bits, copy);
                check_select(bits, mapped_bits, moved);
            }
        }
    }
}

TEST_F(mapped_storage_test, select_mcl_view_storage)
{
    check_mcl_storage<1>(*this, file, 1);
    check_mcl_storage<0>(*this, file, 100);
}

} // namespace
