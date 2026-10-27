// SPDX-License-Identifier: Apache-2.0
//
// Encodes every golden vector from CinematicRAWStudio-Spec/vectors and checks
// the payload is byte-identical to the reference implementation's.
#include <craw/Encoder.h>
#include <craw/Writer.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <regex>
#include <string>
#include <vector>

namespace fs = std::filesystem;

static std::vector<uint8_t> slurp(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), {}};
}

static int fieldInt(const std::string& obj, const char* key) {
    std::smatch m;
    std::regex re(std::string("\"") + key + "\": (\\d+)");
    return std::regex_search(obj, m, re) ? std::stoi(m[1]) : -1;
}

int main(int argc, char** argv) {
    const fs::path dir = argc > 1 ? argv[1] : CRAW_VECTORS_DIR;
    const auto manifestBytes = slurp(dir / "manifest.json");
    const std::string manifest(manifestBytes.begin(), manifestBytes.end());
    std::regex objRe(R"(\{[^{}]*\})");
    int failures = 0, cases = 0;
    for (auto it = std::sregex_iterator(manifest.begin(), manifest.end(), objRe);
         it != std::sregex_iterator(); ++it) {
        const std::string obj = it->str();
        std::smatch m;
        std::regex_search(obj, m, std::regex(R"re("name": "([^"]+)")re"));
        const std::string name = m[1];
        craw::FrameDesc d;
        d.width = fieldInt(obj, "width");
        d.height = fieldInt(obj, "height");
        d.bitDepth = static_cast<uint8_t>(fieldInt(obj, "bit_depth"));
        d.cfa = static_cast<craw::Cfa>(fieldInt(obj, "cfa"));
        d.tile = static_cast<uint16_t>(fieldInt(obj, "tile"));
        d.codec = static_cast<uint8_t>(std::max(1, fieldInt(obj, "codec")));
        d.frameCrc = fieldInt(obj, "frame_crc") != 0;
        const auto rawBytes = slurp(dir / (name + ".raw"));
        std::vector<uint16_t> raw(rawBytes.size() / 2);
        for (size_t i = 0; i < raw.size(); ++i) raw[i] = rawBytes[2 * i] | (rawBytes[2 * i + 1] << 8);
        const auto expected = slurp(dir / (name + ".payload"));
        for (unsigned threads : {1u, 4u}) {
            std::vector<uint8_t> out;
            craw::encodeFrame(raw.data(), d.width, d, out, threads);
            const bool ok = out == expected;
            std::printf("%-6s %s threads=%u (%zu bytes)\n", ok ? "PASS" : "FAIL", name.c_str(),
                        threads, out.size());
            failures += !ok;
        }
        ++cases;
    }
    // Writer smoke test.
    {
        std::vector<uint16_t> frame(64 * 32, 100);
        craw::FrameDesc d{64, 32, 10, craw::Cfa::RGGB, 16, 0};
        const auto path = fs::temp_directory_path() / "craw_writer_test.craw";
        craw::Writer w(path.string(), R"({"sensor":{"width":64,"height":32}})");
        w.addFrame(frame.data(), 64, d, 0);
        craw::ImuSample s{0, 0.1f, 0.2f, 0.3f};
        w.addImu(craw::ImuSensor::Gyroscope, &s, 1);
        float gains[16] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
        w.addShading(0, 1, 2, 2, gains);
        w.close();
        const auto bytes = slurp(path);
        const bool ok = bytes.size() > 16 && std::string(bytes.end() - 8, bytes.end() - 1) == "CRAWEND";
        std::printf("%-6s writer trailer\n", ok ? "PASS" : "FAIL");
        failures += !ok;
    }
    std::printf("%d vectors, %d failures\n", cases, failures);
    return failures || cases == 0;
}
