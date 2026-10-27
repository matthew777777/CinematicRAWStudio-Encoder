// SPDX-License-Identifier: Apache-2.0
//
// Encoder throughput benchmark. Prints one JSON object for device_bench.sh.
//   craw_bench [--input frame.raw --width W --height H] [--bits 10] [--frames 30]
//              [--threads N] [--tile 128] [--codec 1|2]
// Without --input a deterministic synthetic sensor-like frame is used.
#include <craw/Encoder.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <random>
#include <string>
#include <thread>
#include <vector>

#ifdef CRAW_PROFILE
#include <atomic>
namespace craw { extern std::atomic<int64_t> gProfNs[3]; }
#endif

int main(int argc, char** argv) {
    craw::FrameDesc d{4080, 3060, 10, craw::Cfa::RGGB, 128, 0};
    std::string input;
    int frames = 30;
    unsigned threads = std::max(1u, std::thread::hardware_concurrency());
    for (int i = 1; i + 1 < argc; i += 2) {
        const std::string k = argv[i], v = argv[i + 1];
        if (k == "--input") input = v;
        else if (k == "--width") d.width = std::stoul(v);
        else if (k == "--height") d.height = std::stoul(v);
        else if (k == "--bits") d.bitDepth = static_cast<uint8_t>(std::stoi(v));
        else if (k == "--frames") frames = std::stoi(v);
        else if (k == "--threads") threads = std::stoul(v);
        else if (k == "--tile") d.tile = static_cast<uint16_t>(std::stoi(v));
        else if (k == "--codec") d.codec = static_cast<uint8_t>(std::stoi(v));
        else if (k == "--frame-crc") d.frameCrc = v != "0";
        else { std::fprintf(stderr, "unknown option %s\n", k.c_str()); return 2; }
    }
    std::vector<uint16_t> raw(size_t{d.width} * d.height);
    if (!input.empty()) {
        std::ifstream f(input, std::ios::binary);
        f.read(reinterpret_cast<char*>(raw.data()), raw.size() * 2);
        if (!f) { std::fprintf(stderr, "short read %s\n", input.c_str()); return 2; }
    } else {
        std::mt19937 rng(1234);
        std::normal_distribution<float> noise(0.f, 4.f);
        const float maxv = float((1u << d.bitDepth) - 1);
        for (uint32_t y = 0; y < d.height; ++y)
            for (uint32_t x = 0; x < d.width; ++x) {
                float v = maxv * (0.25f + 0.2f * std::sin(x * 0.004f) * std::cos(y * 0.006f)) + noise(rng);
                raw[size_t{y} * d.width + x] = static_cast<uint16_t>(std::clamp(v, 0.f, maxv));
            }
    }
    std::vector<uint8_t> out;
    craw::encodeFrame(raw.data(), d.width, d, out, threads);  // warm-up
    std::vector<double> ms;
    for (int i = 0; i < frames; ++i) {
        const auto t0 = std::chrono::steady_clock::now();
        craw::encodeFrame(raw.data(), d.width, d, out, threads);
        ms.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
    }
#ifdef CRAW_PROFILE
    std::fprintf(stderr, "profile ms/frame: extract %.2f  code %.2f\n",
                 craw::gProfNs[0] / 1e6 / (frames + 1), craw::gProfNs[1] / 1e6 / (frames + 1));
#endif
    std::sort(ms.begin(), ms.end());
    const double p50 = ms[ms.size() / 2], p99 = ms[std::min(ms.size() - 1, ms.size() * 99 / 100)];
    const double mpix = double(d.width) * d.height / 1e6;
    std::printf("{\"tool\":\"craw_bench\",\"width\":%u,\"height\":%u,\"bits\":%u,\"threads\":%u,"
                "\"frames\":%d,\"p50_ms\":%.3f,\"p99_ms\":%.3f,\"mpix_per_s\":%.1f,"
                "\"fps\":%.2f,\"bpp\":%.3f,\"ratio\":%.3f}\n",
                d.width, d.height, d.bitDepth, threads, frames, p50, p99, mpix / (p50 / 1e3),
                1e3 / p50, 8.0 * out.size() / (double(d.width) * d.height),
                double(d.bitDepth) * d.width * d.height / 8.0 / out.size());
}
