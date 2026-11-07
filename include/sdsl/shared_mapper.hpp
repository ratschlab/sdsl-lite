#ifndef SDSL_SHARED_MAPPER_HPP
#define SDSL_SHARED_MAPPER_HPP
#include <sdsl/int_vector_mapper.hpp>
#include <memory>

namespace sdsl {
// Use only upstream mapper operations: plain byte mapping, read-only data and
// lifetime management. Embedded-vector parsing remains in the legacy loaders.
class shared_mapper_ifstream : public mmap_ifstream {
    static std::shared_ptr<mmap_context> map(const std::string &filename) {
        using mapper = int_vector_mapper<8, std::ios_base::in>;
        auto owner = std::make_shared<const mapper>(filename, true);
        return std::make_shared<mmap_context>(filename, owner,
            reinterpret_cast<const uint8_t *>(owner->data()), owner->size());
    }
public:
    explicit shared_mapper_ifstream(const std::string &filename,
                                   std::ios_base::openmode mode = std::ios_base::in)
        : mmap_ifstream(filename, mode, [&] { return map(filename); }) {}
};
}
#endif
