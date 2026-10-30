// SPDX-License-Identifier: Apache-2.0
//
// craw_enc out.craw --width W --height H --bits B [--cfa RGGB] [--fps 30]
//          [--meta meta.json] frame0.raw [frame1.raw ...]
// Each input is one row-major frame of uint16 little-endian samples; a
// sidecar with the same stem and .json extension becomes its frame JSON.
#include <craw/Writer.h>

#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: craw_enc out.craw --width W --height H --bits B "
                             "[--cfa RGGB|GRBG|GBRG|BGGR|RGB] [--fps 30] [--codec 1|2|3|4] [--meta meta.json] frames.raw...\n");
        for (uint8_t codec = 1; codec <= 4; ++codec)
            std::fprintf(stderr, "  --codec %u: %s\n", codec, craw::codecDisplayName(codec));
        return 2;
    }
    craw::FrameDesc d;
    std::string meta = "{}";
    double fps = 30;
    std::vector<std::string> inputs;
    for (int i = 2; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&] { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
        if (a == "--width") d.width = std::stoul(next());
        else if (a == "--height") d.height = std::stoul(next());
        else if (a == "--bits") d.bitDepth = static_cast<uint8_t>(std::stoi(next()));
        else if (a == "--fps") fps = std::stod(next());
        else if (a == "--codec") d.codec = static_cast<uint8_t>(std::stoi(next()));
        else if (a == "--cfa") {
            const std::string c = next();
            d.cfa = c == "GRBG" ? craw::Cfa::GRBG : c == "GBRG" ? craw::Cfa::GBRG
                  : c == "BGGR" ? craw::Cfa::BGGR : c == "RGB" ? craw::Cfa::RGB : craw::Cfa::RGGB;
        } else if (a == "--meta") {
            std::ifstream f(next());
            meta.assign(std::istreambuf_iterator<char>(f), {});
        } else inputs.push_back(a);
    }
    try {
        craw::Writer w(argv[1], meta, std::max(1u, std::thread::hardware_concurrency()));
        std::vector<uint16_t> raw(size_t{d.width} * d.height);
        for (size_t i = 0; i < inputs.size(); ++i) {
            std::ifstream f(inputs[i], std::ios::binary);
            f.read(reinterpret_cast<char*>(raw.data()), raw.size() * 2);
            if (!f) { std::fprintf(stderr, "short read: %s\n", inputs[i].c_str()); return 1; }
            std::string frameMeta = "{}";
            const auto dot = inputs[i].rfind('.');
            if (std::ifstream side(inputs[i].substr(0, dot) + ".json"); side) frameMeta.assign(std::istreambuf_iterator<char>(side), {});
            w.addFrame(raw.data(), d.width, d, static_cast<int64_t>((i + 1) * 1e9 / fps), frameMeta);
        }
        w.close();
    } catch (const std::exception& e) {
        std::fprintf(stderr, "craw_enc: %s\n", e.what());
        return 1;
    }
}
