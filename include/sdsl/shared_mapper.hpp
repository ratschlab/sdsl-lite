#ifndef SDSL_SHARED_MAPPER_HPP
#define SDSL_SHARED_MAPPER_HPP
#include <memory>
#include <stdexcept>
#include <string>

#include <sdsl/int_vector_mapper.hpp>

namespace sdsl
{
// An input file stream that also maps the whole file read-only.
// Loaders of types with view storage take their payloads from mapper()
// instead of copying them. Types with owned storage read the stream as usual.
class shared_mapper_ifstream : public mmap_ifstream
{
public:
    using byte_mapper = int_vector_mapper<8, std::ios_base::in>;

    explicit shared_mapper_ifstream(std::string const & filename, std::ios_base::openmode mode = std::ios_base::in) :
        mmap_ifstream(filename, mode, defer_mapping{}),
        m_filename(filename)
    {
        if (!good())
            return;
        m_mapper = std::make_shared<byte_mapper const>(filename, true);
        set_mmap_context(std::make_shared<mmap_context>(
            filename, m_mapper, reinterpret_cast<uint8_t const *>(m_mapper->data()), m_mapper->size()));
    }

    //! The mapping of the whole file, or null if the file could not be opened.
    std::shared_ptr<byte_mapper const> const & mapper() const noexcept
    {
        return m_mapper;
    }

    std::string const & filename() const noexcept
    {
        return m_filename;
    }

private:
    std::string m_filename;
    std::shared_ptr<byte_mapper const> m_mapper;
};
} // namespace sdsl
#endif
