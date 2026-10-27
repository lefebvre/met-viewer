#pragma once

// Writes small NetCDF-4 files at test time, so a reader test can state the exact
// layout it exercises (a variable missing a dimension, a transposed axis order, a
// string-typed attribute) next to the assertions, instead of depending on a
// checked-in binary built by a separate generator.

#include <netcdf.h>

#include <atomic>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

namespace met::testing {

class NcBuilder {
public:
    explicit NcBuilder(const std::string& stem) {
        static std::atomic<int> counter{0};
        path_ = std::filesystem::temp_directory_path() /
                (stem + "_" + std::to_string(++counter) + ".nc");
        check(nc_create(path_.string().c_str(), NC_NETCDF4 | NC_CLOBBER, &ncid_), "nc_create");
    }
    ~NcBuilder() {
        if (open_) nc_close(ncid_);
        std::error_code ec;
        std::filesystem::remove(path_, ec);
    }
    NcBuilder(const NcBuilder&) = delete;
    NcBuilder& operator=(const NcBuilder&) = delete;

    int dim(const char* name, std::size_t len) {
        int id = 0;
        check(nc_def_dim(ncid_, name, len, &id), "nc_def_dim");
        return id;
    }
    int var(const char* name, nc_type type, const std::vector<int>& dims) {
        int id = 0;
        check(nc_def_var(ncid_, name, type, static_cast<int>(dims.size()), dims.data(), &id),
              "nc_def_var");
        return id;
    }
    // A 1-D coordinate variable over its own dimension, with units.
    int coord(const char* name, const std::vector<double>& values, const std::string& units) {
        const int d = dim(name, values.size());
        const int v = var(name, NC_DOUBLE, {d});
        if (!units.empty()) text(v, "units", units);
        pendingDoubles_.push_back({v, values});
        return d;
    }
    // The id of a variable already defined, e.g. a coordinate made by coord().
    int varid(const char* name) const {
        int id = 0;
        check(nc_inq_varid(ncid_, name, &id), "nc_inq_varid");
        return id;
    }
    void text(int varid, const char* name, const std::string& value) {
        check(nc_put_att_text(ncid_, varid, name, value.size(), value.data()), "nc_put_att_text");
    }
    // A string-typed (NC_STRING) attribute, as h5netcdf and h5py write them.
    void string(int varid, const char* name, const std::string& value) {
        const char* p = value.c_str();
        check(nc_put_att_string(ncid_, varid, name, 1, &p), "nc_put_att_string");
    }
    void number(int varid, const char* name, nc_type type, double value) {
        check(nc_put_att_double(ncid_, varid, name, type, 1, &value), "nc_put_att_double");
    }
    void numbers(int varid, const char* name, nc_type type, const std::vector<double>& values) {
        check(nc_put_att_double(ncid_, varid, name, type, values.size(), values.data()),
              "nc_put_att_double");
    }
    // Values written through netcdf-c's conversion to the variable's type.
    void put(int varid, const std::vector<double>& values) {
        pendingDoubles_.push_back({varid, values});
    }
    // Raw shorts, for packed and _Unsigned data where the stored bits matter.
    void putShorts(int varid, const std::vector<short>& values) {
        pendingShorts_.push_back({varid, values});
    }

    // Leaves define mode, writes every pending array, closes, and returns the path.
    std::filesystem::path finish() {
        check(nc_enddef(ncid_), "nc_enddef");
        for (const auto& [v, values] : pendingDoubles_)
            check(nc_put_var_double(ncid_, v, values.data()), "nc_put_var_double");
        for (const auto& [v, values] : pendingShorts_)
            check(nc_put_var_short(ncid_, v, values.data()), "nc_put_var_short");
        check(nc_close(ncid_), "nc_close");
        open_ = false;
        return path_;
    }

private:
    static void check(int status, const char* what) {
        if (status != NC_NOERR)
            throw std::runtime_error(std::string(what) + ": " + nc_strerror(status));
    }

    std::filesystem::path path_;
    int ncid_ = -1;
    bool open_ = true;
    std::vector<std::pair<int, std::vector<double>>> pendingDoubles_;
    std::vector<std::pair<int, std::vector<short>>> pendingShorts_;
};

}  // namespace met::testing
