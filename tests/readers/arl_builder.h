#pragma once

// Writes small synthetic ARL files at test time, mirroring
// tools/make_arl_fixture.py, so a reader test can set the INDX fields it is
// about (the vertical-coordinate flag, a period's minutes, a projected grid)
// next to the assertions. Every data record holds a constant field.

#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace met::testing {

struct ArlLevel {
    double height;
    std::vector<std::string> vars;
};

struct ArlPeriod {
    int iy = 81, im = 11, id = 1, ih = 0;
    int minutes = 0;
    // One data record per (level index, variable), each a constant `value`.
    struct Record {
        int level;
        std::string kvar;
        double value;
    };
    std::vector<Record> records;
};

struct ArlSpec {
    int nx = 20, ny = 10;
    int kflag = 2;
    // POLE_LAT POLE_LON REF_LAT REF_LON SIZE ORIENT TANG_LAT SYNC_XP SYNC_YP
    // SYNC_LAT SYNC_LON (reserved); a 2-degree lat/lon grid at (30N, 250E) by default.
    std::array<double, 12> grid = {90.0, 250.0, 2.0, 2.0,  0.0,   0.0,
                                   0.0,  1.0,   1.0, 30.0, 250.0, 0.0};
    std::vector<ArlLevel> levels = {{0.0, {"T02M"}}, {1000.0, {"TEMP"}}};
    std::vector<ArlPeriod> periods;
};

class ArlFile {
public:
    ArlFile(const std::string& stem, const ArlSpec& spec) {
        path_ = std::filesystem::temp_directory_path() / (stem + ".arl");
        std::ofstream out(path_, std::ios::binary);
        const std::size_t data =
            static_cast<std::size_t>(spec.nx) * static_cast<std::size_t>(spec.ny);
        for (const ArlPeriod& per : spec.periods) {
            out << label(per, "INDX", 0, 0, 0.0);
            std::string h = header(spec, per);
            h.resize(data, ' ');
            out << h;
            for (const auto& rec : per.records) {
                // A constant field packs as var1 followed by zero differences (127).
                out << label(per, rec.kvar, rec.level, 3, rec.value);
                out << std::string(data, static_cast<char>(127));
            }
        }
    }
    ~ArlFile() {
        std::error_code ec;
        std::filesystem::remove(path_, ec);
    }
    ArlFile(const ArlFile&) = delete;
    ArlFile& operator=(const ArlFile&) = delete;
    [[nodiscard]] const std::filesystem::path& path() const { return path_; }

private:
    static std::string fmt(const char* f, double v) {
        char buf[64];
        std::snprintf(buf, sizeof buf, f, v);
        return buf;
    }
    static std::string fmtInt(const char* f, int v) {
        char buf[64];
        std::snprintf(buf, sizeof buf, f, v);
        return buf;
    }
    static std::string label(const ArlPeriod& p, const std::string& kvar, int ll, int nexp,
                             double var1) {
        std::string s = fmtInt("%2d", p.iy) + fmtInt("%2d", p.im) + fmtInt("%2d", p.id) +
                        fmtInt("%2d", p.ih) + fmtInt("%2d", 0) + fmtInt("%2d", ll) +
                        fmtInt("%2d", 99);
        std::string k = kvar;
        k.resize(4, ' ');
        s += k + fmtInt("%4d", nexp) + fmt("%14.7E", 0.0) + fmt("%14.7E", var1);
        return s;
    }
    static std::string header(const ArlSpec& spec, const ArlPeriod& per) {
        std::string h = "TEST" + fmtInt("%3d", 0) + fmtInt("%2d", per.minutes);
        for (double g : spec.grid) h += fmt("%7.2f", g);
        h += fmtInt("%3d", spec.nx) + fmtInt("%3d", spec.ny) +
             fmtInt("%3d", static_cast<int>(spec.levels.size()));
        h += fmtInt("%2d", spec.kflag) + fmtInt("%4d", 0);
        for (const ArlLevel& l : spec.levels) {
            h += fmt("%6.1f", l.height) + fmtInt("%2d", static_cast<int>(l.vars.size()));
            for (const std::string& v : l.vars) {
                std::string name = v;
                name.resize(4, ' ');
                h += name + fmtInt("%3d", 99) + " ";
            }
        }
        return h;
    }

    std::filesystem::path path_;
};

}  // namespace met::testing
