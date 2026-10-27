// SPDX-License-Identifier: Apache-2.0
#include <craw/Encoder.h>

#include "Crc32c.h"

#include <algorithm>
#include <atomic>
#include <bit>
#include <cstring>
#include <stdexcept>
#include <thread>

namespace craw {
namespace {

constexpr int kRiceLimit = 24;
constexpr int kEscapeBits = 24;
constexpr int kMaxK = 20;
constexpr int kContexts = 16;

// (G1, G2, R, B) positions in the 2x2 cell, index = py * 2 + px.
constexpr int kLayout[4][4] = {{1, 2, 0, 3}, {0, 3, 1, 2}, {0, 3, 2, 1}, {1, 2, 3, 0}};

class BitWriter {
public:
    explicit BitWriter(std::vector<uint8_t>& out) : out_(out) {}

    void put(uint32_t value, int bits) {  // bits <= 32
        acc_ = (acc_ << bits) | (value & ((uint64_t{1} << bits) - 1));
        n_ += bits;
        while (n_ >= 8) {
            n_ -= 8;
            out_.push_back(static_cast<uint8_t>(acc_ >> n_));
        }
    }

    void finish() {
        if (n_) out_.push_back(static_cast<uint8_t>(acc_ << (8 - n_)));
        n_ = 0;
    }

private:
    std::vector<uint8_t>& out_;
    uint64_t acc_ = 0;
    int n_ = 0;
};

void putU16(std::vector<uint8_t>& o, uint16_t v) { o.push_back(v & 0xFF); o.push_back(v >> 8); }
void putU32(std::vector<uint8_t>& o, uint32_t v) {
    for (int i = 0; i < 4; ++i) o.push_back(static_cast<uint8_t>(v >> (8 * i)));
}

void encodePlane(BitWriter& bw, const int32_t* p, int w, int h, int32_t firstPred,
                 uint32_t riceInit) {
    uint32_t A[kContexts], N[kContexts];
    std::fill(A, A + kContexts, riceInit);
    std::fill(N, N + kContexts, 1u);
    for (int y = 0; y < h; ++y) {
        const int32_t* row = p + y * w;
        const int32_t* up = row - w;
        for (int x = 0; x < w; ++x) {
            int32_t pred;
            int ctx = 0;
            if (x == 0 && y == 0) {
                pred = firstPred;
            } else {
                int32_t a, b, c;
                if (y == 0) a = b = c = row[x - 1];
                else if (x == 0) a = b = c = up[0];
                else { a = row[x - 1]; b = up[x]; c = up[x - 1]; }
                const int32_t mx = std::max(a, b), mn = std::min(a, b);
                pred = c >= mx ? mn : c <= mn ? mx : a + b - c;
                const uint32_t g = static_cast<uint32_t>(std::abs(a - c) + std::abs(b - c));
                ctx = std::min(static_cast<int>(std::bit_width(g)), kContexts - 1);
            }
            const int32_t e = row[x] - pred;
            const uint32_t u = e >= 0 ? 2u * e : -2u * static_cast<uint32_t>(e) - 1u;
            int k = 0;
            while ((N[ctx] << k) < A[ctx] && k < kMaxK) ++k;
            const uint32_t q = u >> k;
            if (q < kRiceLimit) {
                bw.put(1, static_cast<int>(q) + 1);
                if (k) bw.put(u, k);
            } else {
                bw.put(1, kRiceLimit + 1);
                bw.put(u, kEscapeBits);
            }
            A[ctx] += u;
            if (++N[ctx] == 64) { A[ctx] >>= 1; N[ctx] >>= 1; }
        }
    }
}

struct TileJob {
    const uint16_t* raw;
    size_t stride;
    const FrameDesc* d;
    uint32_t riceInit;
};

void encodeTile(const TileJob& j, uint32_t tx, uint32_t ty, uint32_t tw, uint32_t th,
                std::vector<int32_t>& planes, std::vector<uint8_t>& out) {
    const int* L = kLayout[static_cast<int>(j.d->cfa)];
    const size_t n = size_t{tw} * th;
    planes.resize(4 * n);
    for (int i = 0; i < 4; ++i) {
        const int py = L[i] >> 1, px = L[i] & 1;
        int32_t* dst = planes.data() + i * n;
        for (uint32_t y = 0; y < th; ++y) {
            const uint16_t* src = j.raw + (2 * (ty + y) + py) * j.stride + 2 * tx + px;
            for (uint32_t x = 0; x < tw; ++x) dst[y * tw + x] = src[2 * x];
        }
    }
    // Stored form uses the untransformed planes.
    std::vector<uint8_t> stored;
    stored.reserve(1 + 8 * n);
    stored.push_back(1);
    for (size_t i = 0; i < 4 * n; ++i) putU16(stored, static_cast<uint16_t>(planes[i]));

    for (int i = 1; i < 4; ++i)
        for (size_t s = 0; s < n; ++s) planes[i * n + s] -= planes[s];

    out.clear();
    out.push_back(0);
    BitWriter bw(out);
    for (int i = 0; i < 4; ++i)
        encodePlane(bw, planes.data() + i * n, tw, th,
                    i == 0 ? (1 << (j.d->bitDepth - 1)) : 0, j.riceInit);
    bw.finish();
    if (out.size() >= stored.size()) out.swap(stored);
}

}  // namespace

uint16_t defaultRiceInit(uint8_t bitDepth) {
    return static_cast<uint16_t>(std::max(1, 1 << std::max(bitDepth - 8, 0)) * 4);
}

void encodeFrame(const uint16_t* raw, size_t stride, const FrameDesc& d,
                 std::vector<uint8_t>& out, unsigned threads) {
    if (!raw || d.width == 0 || d.height == 0 || d.width % 2 || d.height % 2 ||
        d.bitDepth < 1 || d.bitDepth > 16 || d.tile == 0 || stride < d.width ||
        static_cast<int>(d.cfa) > 3)
        throw std::invalid_argument("craw::encodeFrame: invalid frame geometry");

    const uint32_t limit = 1u << d.bitDepth;
    uint32_t crc = 0;
    for (uint32_t y = 0; y < d.height; ++y) {
        const uint16_t* row = raw + y * stride;
        uint16_t mx = 0;
        for (uint32_t x = 0; x < d.width; ++x) mx = std::max(mx, row[x]);
        if (mx >= limit) throw std::invalid_argument("craw::encodeFrame: sample exceeds bit depth");
        if constexpr (std::endian::native == std::endian::little) {
            crc = detail::crc32c(row, d.width * 2, crc);
        } else {
            for (uint32_t x = 0; x < d.width; ++x) {
                const uint8_t le[2] = {static_cast<uint8_t>(row[x]), static_cast<uint8_t>(row[x] >> 8)};
                crc = detail::crc32c(le, 2, crc);
            }
        }
    }

    const uint32_t pw = d.width / 2, ph = d.height / 2;
    const uint32_t tilesX = (pw + d.tile - 1) / d.tile, tilesY = (ph + d.tile - 1) / d.tile;
    const uint32_t count = tilesX * tilesY;
    const TileJob job{raw, stride, &d, d.riceInit ? d.riceInit : defaultRiceInit(d.bitDepth)};

    std::vector<std::vector<uint8_t>> tiles(count);
    std::atomic<uint32_t> next{0};
    auto worker = [&] {
        std::vector<int32_t> planes;
        for (uint32_t t; (t = next.fetch_add(1)) < count;) {
            const uint32_t tx = (t % tilesX) * d.tile, ty = (t / tilesX) * d.tile;
            encodeTile(job, tx, ty, std::min<uint32_t>(d.tile, pw - tx),
                       std::min<uint32_t>(d.tile, ph - ty), planes, tiles[t]);
        }
    };
    threads = std::clamp(threads, 1u, count);
    std::vector<std::thread> pool;
    for (unsigned i = 1; i < threads; ++i) pool.emplace_back(worker);
    worker();
    for (auto& t : pool) t.join();

    out.clear();
    out.push_back(1);
    out.push_back(d.bitDepth);
    out.push_back(static_cast<uint8_t>(d.cfa));
    out.push_back(0);
    putU32(out, d.width);
    putU32(out, d.height);
    putU16(out, d.tile);
    putU16(out, static_cast<uint16_t>(job.riceInit));
    putU32(out, crc);
    putU32(out, count);
    uint32_t off = 0;
    putU32(out, 0);
    for (auto& t : tiles) putU32(out, off += static_cast<uint32_t>(t.size()));
    for (auto& t : tiles) out.insert(out.end(), t.begin(), t.end());
}

}  // namespace craw
