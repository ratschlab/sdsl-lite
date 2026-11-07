#include <map>
#include <numeric>
#include <string>
#include <vector>

#include <sdsl/construct_sa.hpp>
#include <sdsl/construct_sa_se.hpp>
#include <sdsl/suffix_arrays.hpp>

#include "common.hpp"

#include <gtest/gtest.h>

using namespace sdsl;
using namespace std;

namespace
{
uint64_t n;
cache_config config;

class sa_construct_test : public ::testing::Test
{
public:

protected:
    sa_construct_test()
    {}

    virtual ~sa_construct_test()
    {}

    // If the constructor and destructor are not enough for setting up
    // and cleaning up each test, you can define the following methods:
    virtual void SetUp()
    {}

    virtual void TearDown()
    {}
};

TEST_F(sa_construct_test, divsufsort)
{
    // Construct SA with divsufsort
    memory_monitor::start();
    construct_config().byte_algo_sa = LIBDIVSUFSORT;
    construct_sa<8>(config);
    memory_monitor::stop();
    {
        int_vector_buffer<> sa_buf(cache_file_name(conf::KEY_SA, config));
        ASSERT_EQ(n, (uint64_t)sa_buf.size());
    }
    // Rename sa-file
    sdsl::rename(cache_file_name(conf::KEY_SA, config), cache_file_name("check_sa", config));
    config.file_map.erase(conf::KEY_SA);
    register_cache_file("check_sa", config);
    cout << "# constructs_space = " << (1.0 * memory_monitor::peak()) / n << " byte per byte, =>"
         << memory_monitor::peak() << " bytes in total" << endl;
}

TEST_F(sa_construct_test, sesais)
{
    // Construct SA with seSAIS
    memory_monitor::start();
    construct_config().byte_algo_sa = SE_SAIS;
    construct_sa<8>(config);
    memory_monitor::stop();
    {
        int_vector_buffer<> sa_buf(cache_file_name(conf::KEY_SA, config));
        ASSERT_EQ(n, (uint64_t)sa_buf.size());
    }
    cout << "# constructs_space = " << (1.0 * memory_monitor::peak()) / n << " byte per byte, =>"
         << memory_monitor::peak() << " bytes in total" << endl;
}

TEST_F(sa_construct_test, compare)
{
    // Load both SAs
    {
        int_vector_buffer<> sa_check(cache_file_name("check_sa", config));
        int_vector_buffer<> sa(cache_file_name(conf::KEY_SA, config));

        // Verify
        ASSERT_EQ(sa_check.size(), sa.size()) << " suffix array size differ";
        for (uint64_t i = 0; i < sa_check.size(); ++i)
        {
            ASSERT_EQ(sa_check[i], sa[i]) << " sa differs at position " << i;
        }
    }
    // Remove all files
    util::delete_all_files(config.file_map);
}

TEST_F(sa_construct_test, mapped_output_alignment)
{
    // Exercise every payload alignment, both native sort widths, and packed output.
    std::string const text = std::string("banana_mississippi") + '\0';
    std::vector<uint64_t> expected(text.size());
    std::iota(expected.begin(), expected.end(), 0);
    std::sort(expected.begin(), expected.end(), [&](auto a, auto b) {
        return text.substr(a) < text.substr(b);
    });
    for (size_t prefix_size = 0; prefix_size < 8; ++prefix_size)
        for (uint8_t width : {5, 32, 64})
        {
            auto file = cache_file_name("aligned_sa", config);
            std::string const prefix(prefix_size, 'x');
            {
                osfstream out(file, std::ios::binary);
                out.write(prefix.data(), prefix.size());
                int_vector<>(text.size(), 0, width).serialize(out);
            }
            {
                int_vector_mapper<> sa(file, false, false, prefix_size);
                auto bytes = reinterpret_cast<unsigned char const *>(text.data());
                if (width == 64)
                    detail::sort_sa_inplace<int64_t>(bytes, text.size(), sa);
                else
                    algorithm::calculate_sa(bytes, text.size(), sa);
                EXPECT_EQ(text.size(), sa.size());
                EXPECT_EQ(width, sa.width());
                for (size_t i = 0; i < expected.size(); ++i)
                    ASSERT_EQ(expected[i], sa[i]);
            }
            isfstream in(file, std::ios::binary);
            std::string got_prefix(prefix_size, '\0');
            in.read(got_prefix.data(), got_prefix.size());
            EXPECT_EQ(prefix, got_prefix);
            int_vector<> loaded;
            loaded.load(in);
            EXPECT_EQ(width, loaded.width());
            ASSERT_EQ(text.size(), loaded.size());
            for (size_t i = 0; i < expected.size(); ++i)
                ASSERT_EQ(expected[i], loaded[i]);
            in.close();
            sdsl::remove(file);
        }
}

} // namespace

int main(int argc, char ** argv)
{
    string test_file;
    string temp_dir;
    string temp_file;
    ::testing::InitGoogleTest(&argc, argv);
    if (init_2_arg_test(argc, argv, "SA_CONSTRUCT", test_file, temp_dir, temp_file) != 0)
    {
        return 1;
    }
    string test_id = to_string(util::pid());
    config = cache_config(false, temp_dir, test_id);
    if (!cache_file_exists(conf::KEY_TEXT, config))
    {
        int_vector<8> text;
        load_vector_from_file(text, test_file, 1);
        if (contains_no_zero_symbol(text, test_file))
        {
            append_zero_symbol(text);
            store_to_cache(text, conf::KEY_TEXT, config);
        }
        n = text.size();
    }
    register_cache_file(conf::KEY_TEXT, config);

    return RUN_ALL_TESTS();
}
