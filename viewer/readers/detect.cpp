#include "viewer/readers/detect.h"

#include <array>
#include <cstddef>
#include <fstream>
#include <vector>

#include "viewer/readers/grib/gribreader.h"
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
        return v;
    }();
    return *readers;
}

std::vector<std::byte> readHead(const std::filesystem::path& path, std::size_t n) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw ReadError("cannot open file: " + path.string());
    std::vector<std::byte> buf(n);
    in.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(n));
    buf.resize(static_cast<std::size_t>(in.gcount()));
    return buf;
}

}  // namespace

std::unique_ptr<IDataset> openDataset(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path)) throw ReadError("no such file: " + path.string());

    const std::vector<std::byte> head = readHead(path, 4096);

    const IFormatReader* best = nullptr;
    int bestScore = 0;
    for (const auto& reader : registry()) {
        const int score = reader->probe(head, path.string());
        if (score > bestScore) {
            bestScore = score;
            best = reader.get();
        }
    }

    if (!best) throw ReadError("no reader recognizes file: " + path.string());
    return best->open(path);
}

}  // namespace met::readers
