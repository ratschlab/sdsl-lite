#ifndef SDSL_UNALIGNED_ACCESS_HPP
#define SDSL_UNALIGNED_ACCESS_HPP
#include <cstdint>
#include <cstring>
#include <type_traits>

#include <sdsl/packed_word_iterator.hpp>

namespace sdsl::detail
{
// Read a value without requiring its serialized address to be aligned.
template <class Word>
inline Word load_unaligned(void const * address) noexcept
{
    static_assert(std::is_trivially_copyable_v<Word>);
    Word value;
    std::memcpy(&value, address, sizeof(value));
    return value;
}

template <class Word>
constexpr Word load_unaligned(Word const * address) noexcept
{
    if (std::is_constant_evaluated())
        return *address;
    return load_unaligned<Word>(static_cast<void const *>(address));
}

// Word iterators over unaligned storage already read through memcpy.
inline uint64_t load_unaligned(packed_word_iterator p) noexcept
{
    return *p;
}

template <class Word>
constexpr void store_unaligned(Word * address, Word value) noexcept
{
    static_assert(std::is_trivially_copyable_v<Word>);
    if (std::is_constant_evaluated())
        *address = value;
    else
        std::memcpy(static_cast<void *>(address), &value, sizeof(value));
}
} // namespace sdsl::detail
#endif
