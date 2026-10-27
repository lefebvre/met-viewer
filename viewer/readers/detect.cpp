#include "viewer/readers/detect.h"

#include <array>
#include <cstddef>
#include <fstream>
#include <memory>
#include <vector>

#include "viewer/core/log.h"
#include "viewer/core/pathutil.h"
#include "viewer/readers/arl/arlreader.h"
#include "viewer/readers/grib/gribreader.h"
#include "viewer/readers/multidataset.h"
#include "viewer/readers/netcdf/cfreader.h"

namespace met::readers {
namespace {

// Compile-time registry of format readers. New formats (ARL) register here in
// later milestones.
const std::vector<std::unique_ptr<IFormatReader>>& registry() {
    static const std::vector<std::unique_ptr<IFormatReader>>* readers = [] {
        auto* v = new std::vector<std::unique_ptr<IFormatReader>>();
        v->push_back(std::make_unique<grib::GribReader>());
        v->push_back(std::make_unique<netcdf::CfReader>());
        v->push_back(std::make_unique<arl::ArlReader>());
        return v;
    }();
    return *readers;
}

std::vector<std::byte> readHead(const std::filesystem::path& path, std::size_t n) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw ReadError("cannot open file: " + core::pathToUtf8(path));
    std::vector<std::byte> buf(n);
    in.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(n));
    buf.resize(static_cast<std::size_t>(in.gcount()));
    return buf;
}

}  // namespace

std::unique_ptr<IDataset> openDataset(const std::filesystem::path& path) {
    // The error_code overloads: the throwing ones raise filesystem_error (a path
    // the OS refuses to stat, a permission problem), which is not the ReadError
    // this function promises and slips past a caller catching that.
    std::error_code ec;
    const auto status = std::filesystem::status(path, ec);
    if (ec || !std::filesystem::exists(status))
        throw ReadError("no such file: " + core::pathToUtf8(path));
    if (std::filesystem::is_directory(status))
        throw ReadError("is a directory, not a file: " + core::pathToUtf8(path));

    const std::vector<std::byte> head = readHead(path, 4096);

    const IFormatReader* best = nullptr;
    int bestScore = 0;
    for (const auto& reader : registry()) {
        const int score = reader->probe(head, core::pathToUtf8(path));
        if (score > bestScore) {
            bestScore = score;
            best = reader.get();
        }
    }

    if (!best) throw ReadError("no reader recognizes file: " + core::pathToUtf8(path));
    core::logf(core::LogLevel::Debug, "{}: opening as {} (probe score {})", core::pathToUtf8(path),
               best->name(), bestScore);
    return best->open(path);
}

OpenResult openDatasets(const std::vector<std::filesystem::path>& paths) {
    if (paths.empty()) throw ReadError("no files to open");

    OpenResult result;
    std::vector<std::shared_ptr<IDataset>> sources;
    sources.reserve(paths.size());
    for (const auto& path : paths) {
        try {
            sources.emplace_back(std::shared_ptr<IDataset>(openDataset(path)));
        } catch (const std::exception& e) {
            core::logf(core::LogLevel::Warn, "skipping {}: {}", core::pathToUtf8(path), e.what());
            result.skipped.push_back(path);
        }
    }

    if (sources.empty()) throw ReadError("none of the selected files could be opened");

    // A single file needs no wrapping — return it directly so single-file behavior
    // (including formatName) is byte-for-byte what it was before.
    result.dataset = sources.size() == 1 ? std::move(sources.front())
                                         : std::make_shared<MultiDataset>(std::move(sources));
    return result;
}

}  // namespace met::readers
