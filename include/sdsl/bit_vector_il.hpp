// Copyright (c) 2016, the SDSL Project Authors.  All rights reserved.
// Please see the AUTHORS file for details.  Use of this source code is governed
// by a BSD license that can be found in the LICENSE file.
/*!\file bit_vector_il.hpp
 * \brief bit_vector_il.hpp contains the sdsl::bit_vector_il class, and
 * classes which support rank and select for bit_vector_il.
 * \author Matthias Petri, Simon Gog
 */
#ifndef SDSL_BIT_VECTOR_IL
#define SDSL_BIT_VECTOR_IL

#include <algorithm>
#include <bit>
#include <assert.h>
#include <iosfwd>
#include <queue>
#include <stdint.h>
#include <string>
#include <type_traits>

#include <sdsl/bit_vector_helper.hpp>
#include <sdsl/bits.hpp>
#include <sdsl/cereal.hpp>
#include <sdsl/int_vector.hpp>
#include <sdsl/io.hpp>
#include <sdsl/iterators.hpp>
#include <sdsl/sdsl_concepts.hpp>
#include <sdsl/structure_tree.hpp>
#include <sdsl/util.hpp>

//! Namespace for the succinct data structure library
namespace sdsl
{

// forward declarations needed for friend declarations
template <uint8_t t_b = 1, uint32_t t_bs = 512, template <uint8_t> class t_storage = int_vector>
class rank_support_il;
template <uint8_t t_b = 1, uint32_t t_bs = 512, template <uint8_t> class t_storage = int_vector>
class select_support_il;

template <class T>
constexpr bool power_of_two(T x)
{
    return std::is_integral<T>::value and x > 1 and !(x & (x - 1));
}

//! A bit vector which interleaves the original bit_vector with rank information.
/*!
 * This class is a uncompressed bit vector representation. It copies the original
 * bit_vector and interleaves the data every t_bs bits with a cumulative
 * sum of set bits before the current position. Each cumulative sum is stored
 * in a 64 bit word.
 *
 * \tparam t_bs Block size in bits. t_bs has to be a power of 2 and t_bs >= 64.
 */
template <uint32_t t_bs = 512, template <uint8_t> class t_storage = int_vector>
class bit_vector_il
{
    static_assert(t_bs >= 64, "bit_vector_il: blocksize must be be at least 64 bits.");
    static_assert(power_of_two(t_bs), "bit_vector_il: blocksize must be a power of two.");

public:
    typedef bit_vector::size_type size_type;
    typedef size_type value_type;
    typedef bit_vector::difference_type difference_type;
    typedef random_access_const_iterator<bit_vector_il> iterator;
    typedef iterator const_iterator;
    typedef bv_tag index_category;

    friend class rank_support_il<1, t_bs, t_storage>;
    friend class rank_support_il<0, t_bs, t_storage>;
    friend class select_support_il<1, t_bs, t_storage>;
    friend class select_support_il<0, t_bs, t_storage>;

    typedef rank_support_il<1, t_bs, t_storage> rank_1_type;
    typedef rank_support_il<0, t_bs, t_storage> rank_0_type;
    typedef select_support_il<1, t_bs, t_storage> select_1_type;
    typedef select_support_il<0, t_bs, t_storage> select_0_type;

private:
    size_type m_size = 0;        //!< Size of the original bitvector
    size_type m_block_num = 0;   //!< Total size of m_data in uint64_t ss
    size_type m_superblocks = 0; //!< Number of superblocks
    size_type m_block_shift = 0;
    t_storage<64> m_data;         //!< Data container
    t_storage<64> m_rank_samples; //!< Space for additional rank samples

    // The payload from the given word on, as bytes: view storage need not be word-aligned.
    std::span<std::byte const> words_from(size_type word) const
    {
        if constexpr (requires { m_data.bytes(); })
            return m_data.bytes().subspan(8 * word);
        else
            return std::as_bytes(std::span(m_data.data(), m_data.size())).subspan(8 * word);
    }

    template <uint32_t, template <uint8_t> class>
    friend class bit_vector_il;

    // precondition: m_rank_samples.size() <= m_superblocks
    void init_rank_samples()
    {
        uint32_t blockSize_U64 = bits::hi(t_bs >> 6);
        size_type idx = 0;
        std::queue<size_type> lbs, rbs;
        lbs.push(0);
        rbs.push(m_superblocks);
        while (!lbs.empty())
        {
            size_type lb = lbs.front();
            lbs.pop();
            size_type rb = rbs.front();
            rbs.pop();
            if (/*lb < rb and*/ idx < m_rank_samples.size())
            {
                size_type mid = lb + (rb - lb) / 2; // select mid \in [lb..rb)
                size_type pos = (mid << blockSize_U64) + mid;
                m_rank_samples[idx++] = m_data[pos];
                lbs.push(lb);
                rbs.push(mid);
                lbs.push(mid + 1);
                rbs.push(rb);
            }
        }
    }

public:
    bit_vector_il()
    {}
    bit_vector_il(bit_vector_il const &) = default;
    bit_vector_il(bit_vector_il &&) = default;
    bit_vector_il & operator=(bit_vector_il const &) = default;
    bit_vector_il & operator=(bit_vector_il &&) = default;

    bit_vector_il(bit_vector const & bv)
    {
        if constexpr (std::is_same_v<t_storage<64>, int_vector<64>>)
        {
            construct(bv);
        }
        else
        {
            bit_vector_il<t_bs> owned(bv);
            m_size = owned.m_size;
            m_block_num = owned.m_block_num;
            m_superblocks = owned.m_superblocks;
            m_block_shift = owned.m_block_shift;
            m_data = t_storage<64>(std::move(owned.m_data));
            m_rank_samples = t_storage<64>(std::move(owned.m_rank_samples));
        }
    }

private:
    void construct(bit_vector const & bv)
    {
        m_size = bv.size();
        /* calculate the number of superblocks */
        //          each block of size > 0 gets suberblock in which we store the cumulative sum up to this block
        m_superblocks = (m_size + t_bs) / t_bs;
        m_block_shift = bits::hi(t_bs);
        /* allocate new data */
        size_type words = (m_size + 63) / 64;
        bool const padding_word = m_size % 64 == 0;
        // Stored words, cumulative rank samples, and the final population count.
        size_type mem = words + padding_word + m_superblocks + 1;
        m_data = int_vector<64>(mem);
        m_block_num = mem;

        /* assign data and calculate super block values */
        uint64_t const * bvp = bv.data();

        size_type j = 0; // 64-bit word counter in the m_data
        size_type cum_sum = 0;
        size_type sample_rate = t_bs / 64;
        size_type sample_cnt = sample_rate;
        auto append_word = [&](uint64_t word)
        {
            if (sample_cnt == sample_rate)
            {
                m_data[j++] = cum_sum;
                sample_cnt = 0;
            }
            m_data[j] = word;
            cum_sum += bits::cnt(m_data[j]);
            ++j;
            ++sample_cnt;
        };
        for (size_type i = 0; i + 1 < words; ++i)
            append_word(bvp[i]);
        if (words)
            append_word(bvp[words - 1] & bits::lo_set[m_size - (words - 1) * 64]);
        if (padding_word)
            append_word(0);  // Preserve the layout's extra word at exact word boundaries.
        m_data[j] = cum_sum; /* last superblock so we can always
                                    get num_ones fast */
        if (m_block_num > 1024 * 64)
        {
            // we store at most m_superblocks+1 rank_samples:
            // we do a cache efficient binary search for the select on X=1024
            // or X=the smallest power of two smaller than m_superblock
            m_rank_samples.resize(std::min(1024ULL, 1ULL << bits::hi(m_superblocks)));
        }
        init_rank_samples();
    }

public:
    //! Accessing the i-th element of the original bit_vector
    /*!\param i An index i with \f$ 0 \leq i < size()  \f$.
     *  \return The i-th bit of the original bit_vector
     *  \par Time complexity
     *     \f$ \Order{1} \f$
     */
    value_type operator[](size_type i) const
    {
        assert(i < m_size);
        size_type bs = i >> m_block_shift;
        size_type block = bs + (i >> 6) + 1;
        return ((m_data[block] >> (i & 63)) & 1ULL);
    }

    //! Get the integer value of the binary string of length len starting at position idx.
    /*!\param idx Starting index of the binary representation of the integer.
     *  \param len Length of the binary representation of the integer. Default value is 64.
     *   \returns The integer value of the binary string of length len starting at position idx.
     *
     *  \pre idx+len-1 in [0..size()-1]
     *  \pre len in [1..64]
     */
    uint64_t get_int(size_type idx, uint8_t len = 64) const
    {
        assert(idx + len - 1 < m_size);
        size_type bs = idx >> m_block_shift;
        size_type b_block = bs + (idx >> 6) + 1;
        bs = (idx + len - 1) >> m_block_shift;
        size_type e_block = bs + ((idx + len - 1) >> 6) + 1;
        if (b_block == e_block)
        { // spans on block
            return (m_data[b_block] >> (idx & 63)) & bits::lo_set[len];
        }
        else
        { // spans two blocks
            uint8_t b_len = 64 - (idx & 63);
            return (m_data[b_block] >> (idx & 63)) | (m_data[e_block] & bits::lo_set[len - b_len]) << b_len;
        }
    }

    //! Returns the size of the original bit vector.
    size_type size() const
    {
        return m_size;
    }

    //! Serializes the data structure into the given ostream
    size_type serialize(std::ostream & out, structure_tree_node * v = nullptr, std::string name = "") const
    {
        structure_tree_node * child = structure_tree::add_child(v, name, util::class_name(*this));
        size_type written_bytes = 0;
        written_bytes += write_member(m_size, out, child, "size");
        written_bytes += write_member(m_block_num, out, child, "block_num");
        written_bytes += write_member(m_superblocks, out, child, "superblocks");
        written_bytes += write_member(m_block_shift, out, child, "block_shift");
        written_bytes += m_data.serialize(out, child, "data");
        written_bytes += m_rank_samples.serialize(out, child, "rank_samples");
        structure_tree::add_size(child, written_bytes);
        return written_bytes;
    }

    //! Loads the data structure from the given istream.
    void load(std::istream & in)
    {
        read_member(m_size, in);
        read_member(m_block_num, in);
        read_member(m_superblocks, in);
        read_member(m_block_shift, in);
        m_data.load(in);
        m_rank_samples.load(in);
    }

    template <typename archive_t>
    void CEREAL_SAVE_FUNCTION_NAME(archive_t & ar) const
    {
        ar(CEREAL_NVP(m_size));
        ar(CEREAL_NVP(m_block_num));
        ar(CEREAL_NVP(m_superblocks));
        ar(CEREAL_NVP(m_block_shift));
        ar(CEREAL_NVP(m_data));
        ar(CEREAL_NVP(m_rank_samples));
    }

    template <typename archive_t, std::enable_if_t<!std::is_base_of_v<std::istream, archive_t>, int> = 0>
    void CEREAL_LOAD_FUNCTION_NAME(archive_t & ar)
    {
        ar(CEREAL_NVP(m_size));
        ar(CEREAL_NVP(m_block_num));
        ar(CEREAL_NVP(m_superblocks));
        ar(CEREAL_NVP(m_block_shift));
        ar(CEREAL_NVP(m_data));
        ar(CEREAL_NVP(m_rank_samples));
    }

    iterator begin() const
    {
        return iterator(this, 0);
    }

    iterator end() const
    {
        return iterator(this, size());
    }

    bool operator==(bit_vector_il const & v) const
    {
        return m_size == v.m_size && m_data == v.m_data;
    }

    bool operator!=(bit_vector_il const & v) const
    {
        return !(*this == v);
    }
};

template <uint8_t t_b, uint32_t t_bs, template <uint8_t> class t_storage>
class rank_support_il
{
    static_assert(t_b == 1 or t_b == 0, "rank_support_il only supports bitpatterns 0 or 1.");

public:
    typedef bit_vector::size_type size_type;
    typedef bit_vector_il<t_bs, t_storage> bit_vector_type;
    enum
    {
        bit_pat = t_b
    };
    enum
    {
        bit_pat_len = (uint8_t)1
    };

private:
    bit_vector_type const * m_v;
    static constexpr size_type m_block_shift = std::countr_zero(t_bs);
    static constexpr size_type m_block_mask = t_bs - 1;
    static constexpr size_type m_block_size_U64 = std::countr_zero(t_bs / 64);

    inline size_type rank1(size_type i) const
    {
        size_type SBlockNum = i >> m_block_shift;
        size_type SBlockPos = (SBlockNum << m_block_size_U64) + SBlockNum;
        auto words = m_v->words_from(SBlockPos + 1);
        return detail::rank_in_words(words, i & m_block_mask, m_v->m_data[SBlockPos]);
    }

    inline size_type rank0(size_type i) const
    {
        return i - rank1(i);
    }

public:
    rank_support_il(bit_vector_type const * v = nullptr)
    {
        set_vector(v);
    }

    //! Returns the position of the i-th occurrence in the bit vector.
    size_type rank(size_type i) const
    {
        if (t_b)
            return rank1(i);
        return rank0(i);
    }

    size_type operator()(size_type i) const
    {
        return rank(i);
    }

    size_type size() const
    {
        return m_v->size();
    }

    void set_vector(bit_vector_type const * v = nullptr)
    {
        m_v = v;
    }

    rank_support_il & operator=(rank_support_il const & rs)
    {
        if (this != &rs)
        {
            set_vector(rs.m_v);
        }
        return *this;
    }

    void load(std::istream &, bit_vector_type const * v = nullptr)
    {
        set_vector(v);
    }

    size_type serialize(std::ostream & out, structure_tree_node * v = nullptr, std::string name = "") const
    {
        return serialize_empty_object(out, v, name, this);
    }

    template <typename archive_t>
    void CEREAL_SAVE_FUNCTION_NAME(archive_t &) const
    {}

    template <typename archive_t>
    void CEREAL_LOAD_FUNCTION_NAME(archive_t &)
    {}

    bool operator==(rank_support_il const & other) const noexcept
    {
        return (*m_v == *other.m_v);
    }

    bool operator!=(rank_support_il const & other) const noexcept
    {
        return !(*this == other);
    }
};

template <uint8_t t_b, uint32_t t_bs, template <uint8_t> class t_storage>
class select_support_il
{
    static_assert(t_b == 1 or t_b == 0, "select_support_il only supports bitpatterns 0 or 1.");

public:
    typedef bit_vector::size_type size_type;
    typedef bit_vector_il<t_bs, t_storage> bit_vector_type;
    enum
    {
        bit_pat = t_b
    };
    enum
    {
        bit_pat_len = (uint8_t)1
    };

private:
    bit_vector_type const * m_v;
    static constexpr size_type m_block_shift = std::countr_zero(t_bs);
    static constexpr size_type m_block_size_U64 = std::countr_zero(t_bs / 64);

    size_type count_before(size_type block, size_type ones) const
    {
        if constexpr (t_b)
            return ones;
        else
            return (block << m_block_shift) - ones;
    }

public:
    select_support_il(bit_vector_type const * v = nullptr)
    {
        set_vector(v);
    }

    //! Returns the position of the i-th occurrence in the bit vector.
    size_type select(size_type i) const
    {
        size_type lb = 0, rb = m_v->m_superblocks; // search interval [lb..rb)
#ifndef NOSELCACHE
        size_type idx = 0; // index in m_rank_samples
#endif
        // Find the first block boundary with at least i matching bits before it.
        while (lb < rb)
        {
            size_type mid = (lb + rb) / 2; // select mid \in [lb..rb)
#ifndef NOSELCACHE
            if (idx < m_v->m_rank_samples.size())
            {
                if (count_before(mid, m_v->m_rank_samples[idx]) >= i)
                {
                    idx = (idx << 1) + 1;
                    rb = mid;
                }
                else
                {
                    idx = (idx << 1) + 2;
                    lb = mid + 1;
                }
            }
            else
            {
#endif
                size_type pos = (mid << m_block_size_U64) + mid;
                //                                  ^^^^^^^^^^^^^^^^^^^^^^     ^^^^^^^^^^^^^^^^^^^
                //                                    data blocks to jump      superblock position
                if (count_before(mid, m_v->m_data[pos]) >= i)
                {
                    rb = mid;
                }
                else
                {
                    lb = mid + 1;
                }
#ifndef NOSELCACHE
            }
#endif
        }
        size_type block = rb - 1;
        size_type offset = (block << m_block_size_U64) + block;
        i -= count_before(block, m_v->m_data[offset]);
        auto words = m_v->words_from(offset + 1);
        return (block << m_block_shift) + detail::select_in_words<t_b>(words, i);
    }

    size_type operator()(size_type i) const
    {
        return select(i);
    }

    size_type size() const
    {
        return m_v->size();
    }

    void set_vector(bit_vector_type const * v = nullptr)
    {
        m_v = v;
    }

    select_support_il & operator=(select_support_il const & rs)
    {
        if (this != &rs)
        {
            set_vector(rs.m_v);
        }
        return *this;
    }

    void load(std::istream &, bit_vector_type const * v = nullptr)
    {
        set_vector(v);
    }

    size_type serialize(std::ostream & out, structure_tree_node * v = nullptr, std::string name = "") const
    {
        return serialize_empty_object(out, v, name, this);
    }

    template <typename archive_t>
    void CEREAL_SAVE_FUNCTION_NAME(archive_t &) const
    {}

    template <typename archive_t>
    void CEREAL_LOAD_FUNCTION_NAME(archive_t &)
    {}

    bool operator==(select_support_il const & other) const noexcept
    {
        return (*m_v == *other.m_v);
    }

    bool operator!=(select_support_il const & other) const noexcept
    {
        return !(*this == other);
    }
};

} // end namespace sdsl
#endif
