#pragma once

// Builds small GRIB2 messages at test time from ecCodes' GRIB2 sample, so a reader
// test can state the exact level type or statistical window it exercises next to
// the assertions about it.

#include <eccodes.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

namespace met::testing {

// A 4x3 regular lat/lon message whose every value is `value`, customized by `setup`
// (which sets level type, parameter, step range, ...). Returned as raw bytes.
inline std::string gribMessage(double value, const std::function<void(codes_handle*)>& setup) {
    codes_handle* h = codes_grib_handle_new_from_samples(nullptr, "GRIB2");
    if (!h) throw std::runtime_error("cannot load the GRIB2 sample");
    auto check = [h](int err, const char* what) {
        if (err != CODES_SUCCESS) {
            codes_handle_delete(h);
            throw std::runtime_error(std::string(what) + ": " + codes_get_error_message(err));
        }
    };
    check(codes_set_long(h, "Ni", 4), "Ni");
    check(codes_set_long(h, "Nj", 3), "Nj");
    check(codes_set_double(h, "latitudeOfFirstGridPointInDegrees", 20.0), "lat0");
    check(codes_set_double(h, "longitudeOfFirstGridPointInDegrees", 0.0), "lon0");
    check(codes_set_double(h, "latitudeOfLastGridPointInDegrees", 0.0), "lat1");
    check(codes_set_double(h, "longitudeOfLastGridPointInDegrees", 30.0), "lon1");
    check(codes_set_double(h, "iDirectionIncrementInDegrees", 10.0), "di");
    check(codes_set_double(h, "jDirectionIncrementInDegrees", 10.0), "dj");
    setup(h);
    const std::vector<double> values(12, value);
    check(codes_set_double_array(h, "values", values.data(), values.size()), "values");
    const void* buf = nullptr;
    std::size_t size = 0;
    check(codes_get_message(h, &buf, &size), "get_message");
    std::string out(static_cast<const char*>(buf), size);
    codes_handle_delete(h);
    return out;
}

inline void setString(codes_handle* h, const char* key, const std::string& v) {
    std::size_t len = v.size();
    const int err = codes_set_string(h, key, v.c_str(), &len);
    if (err != CODES_SUCCESS)
        throw std::runtime_error(std::string(key) + "=" + v + ": " + codes_get_error_message(err));
}

inline void setLong(codes_handle* h, const char* key, long v) {
    const int err = codes_set_long(h, key, v);
    if (err != CODES_SUCCESS)
        throw std::runtime_error(std::string(key) + ": " + codes_get_error_message(err));
}

// Writes the concatenated messages to a temporary file, removed on destruction.
class GribFile {
public:
    GribFile(const std::string& stem, const std::vector<std::string>& messages) {
        path_ = std::filesystem::temp_directory_path() / (stem + ".grib2");
        std::ofstream out(path_, std::ios::binary);
        for (const std::string& m : messages)
            out.write(m.data(), static_cast<std::streamsize>(m.size()));
    }
    ~GribFile() {
        std::error_code ec;
        std::filesystem::remove(path_, ec);
    }
    GribFile(const GribFile&) = delete;
    GribFile& operator=(const GribFile&) = delete;
    [[nodiscard]] const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

}  // namespace met::testing
