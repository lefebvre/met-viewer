// libFuzzer harness for the ARL reader.
//
// ARL is the one format here that is decoded by hand: fixed-width Fortran text
// labels, a variable-length level table whose entries walk a cursor, and 1-byte
// scaled running differences, all from a file layout that is only semi-documented.
// Every other reader delegates to a hardened library (ecCodes, netcdf-c/HDF5).
// That makes this the file where a malformed input can walk an offset off the end
// of a buffer, and it has already produced one real misalignment bug (dimensions
// above 999, fixed by the grid-ID thousands decode).
//
// What counts as a finding: a crash, a hang, or a sanitizer report. A ReadError is
// the *correct* outcome for a malformed input and is caught below — the reader is
// supposed to refuse bad files loudly, and this harness asserts it does so without
// reaching for memory it does not own.
//
// Build and run (Clang; libFuzzer is not available under GCC or MSVC):
//
//   cmake --preset fuzz -DCMAKE_CXX_COMPILER=clang++
//   cmake --build --preset fuzz
//   ./build/fuzz/tests/fuzz/arl_fuzz -max_total_time=300 tests/fuzz/corpus
//
// Seed the corpus from the checked-in fixture:
//
//   mkdir -p tests/fuzz/corpus && cp tests/fixtures/small_latlon.arl tests/fuzz/corpus/

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include "viewer/readers/arl/arldataset.h"
#include "viewer/readers/ireader.h"

namespace {

// ArlDataset reads from a path, not a buffer, so each input round-trips through a
// temp file. That is slower than an in-memory harness would be, but it exercises
// the real seek/stride arithmetic over a real file size — which is precisely where
// the offset bugs this is hunting for live. Reusing one path keeps the cost to a
// rewrite rather than a create/unlink per input.
const std::filesystem::path& scratchPath() {
    static const std::filesystem::path p = [] {
        std::filesystem::path dir = std::filesystem::temp_directory_path() / "met_arl_fuzz";
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        return dir / "input.arl";
    }();
    return p;
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    // A record is NX*NY + 50 bytes, so anything shorter than one label cannot be a
    // file at all and only costs time.
    if (size < 50) return 0;

    {
        std::ofstream out(scratchPath(), std::ios::binary | std::ios::trunc);
        if (!out) return 0;
        out.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
    }

    try {
        met::readers::arl::ArlDataset ds(scratchPath());

        // Opening only exercises scan(). Decode every catalogued record too: that
        // is the path through unpack(), and the one that trusts nx_/ny_ and the
        // record offsets the header produced.
        const auto& catalog = ds.catalog();
        for (const auto& var : catalog.variables()) {
            for (const auto& level : var.levels) {
                for (const auto& time : var.times) {
                    met::core::FieldKey key;
                    key.varName = var.varName;
                    key.level = level;
                    key.validTime = time;
                    try {
                        (void)ds.readField(key);
                    } catch (const met::readers::ReadError&) {
                        // Expected for a truncated or inconsistent record.
                    }
                }
            }
        }
    } catch (const met::readers::ReadError&) {
        // Expected: refusing a malformed file is the contract, not a failure.
    }
    return 0;
}
