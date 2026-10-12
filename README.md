# SDSL v3 - Succinct Data Structure Library

[![CI][1]][2]

[1]: https://img.shields.io/github/actions/workflow/status/xxsds/sdsl-lite/ci_linux.yml?branch=master&style=flat&logo=github&label=CI "Open GitHub actions page"
[2]: https://github.com/xxsds/sdsl-lite/actions?query=branch%3Amaster+event%3Apush

## MetaGraph lab integration

This branch follows upstream v3 and includes the changes submitted in xxsds/sdsl-lite PRs #145–158 and #160.
It retains the lab fork's integer-vector, RRR, mapped-select, and count-vector formats so existing
MetaGraph indexes remain readable. These stream formats differ from unmodified upstream v3.
The lab's mapped-file APIs and embedded-vector offsets are also retained.

SDSL is now header-only and requires C++17 or later. V3 adds per-vector capacity and growth bookkeeping;
on x86-64, `int_vector` objects grow from 40 to 48 bytes. Serialized payload sizes do not grow with it.
Retained v2 implementations keep their original GPL notices; [COPYING.v2](COPYING.v2) contains that license.
Upstream v3's license remains in [LICENSE](LICENSE).

## Main differences to [v2](https://github.com/simongog/sdsl-lite)

* header-only library
* support for serialisation via [cereal](https://github.com/USCiLab/cereal)
* compatible with C++17, C++20, and C++23

## Supported compilers

Other compiler may work, but are not tested within the continuous integration. In general, the latest minor release of
each listed major compiler version is supported.

* GCC 14, 15, 16
* clang 20, 21, 22

Tests are run with C++23.

## Dependencies

As SDSL v3 is header-only, dependencies marked as `required` only apply to building tests/examples.

GoogleTest and cereal are provided as submodules within this repository.

* required: [CMake >= 3.13](https://github.com/Kitware/CMake)
* required: [GoogleTest 1.17.0](https://github.com/google/googletest/releases/tag/v1.17.0)
* optional: [cereal 1.3.2](https://github.com/USCiLab/cereal/releases/tag/v1.3.2)

cereal can be activated by passing `-DSDSL_CEREAL=1` to CMake.
