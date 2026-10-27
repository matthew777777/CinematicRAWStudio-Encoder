// SPDX-License-Identifier: Apache-2.0
#include <craw/Writer.h>

#include "Crc32c.h"

#include <cstring>
#include <stdexcept>

namespace craw {
namespace {

template <typename T>
void put(std::vector<uint8_t>& o, T v) {
    for (size_t i = 0; i < sizeof(T); ++i) o.push_back(static_cast<uint8_t>(uint64_t(v) >> (8 * i)));
}

void putF32(std::vector<uint8_t>& o, float f) {
    uint32_t u;
    std::memcpy(&u, &f, 4);
    put(o, u);
}

void putBytes(std::vector<uint8_t>& o, const void* p, size_t n) {
    auto b = static_cast<const uint8_t*>(p);
    o.insert(o.end(), b, b + n);
}

}  // namespace

Writer::Writer(const std::string& path, std::string_view metaJson, unsigned threads)
    : threads_(threads) {
    f_ = std::fopen(path.c_str(), "wb");
    if (!f_) throw std::runtime_error("craw::Writer: cannot open " + path);
    std::vector<uint8_t> head{'C', 'R', 'A', 'W'};
    put<uint16_t>(head, 0);
    put<uint16_t>(head, 1);
    put<uint32_t>(head, 0);
    put<uint32_t>(head, 0);
    if (std::fwrite(head.data(), 1, head.size(), f_) != head.size())
        throw std::runtime_error("craw::Writer: write failed");
    pos_ = head.size();
    scratch_.assign(metaJson.begin(), metaJson.end());
    putChunk("META", 0, scratch_);
    index_.clear();  // META is not indexed
}

Writer::~Writer() {
    if (f_) {
        try { close(); } catch (...) {}
    }
}

void Writer::putChunk(const char* fourcc, int64_t ts, const std::vector<uint8_t>& payload) {
    if (!f_) throw std::runtime_error("craw::Writer: closed");
    IndexEntry e{};
    std::memcpy(e.fourcc, fourcc, 4);
    e.offset = pos_;
    e.ts = ts;
    std::vector<uint8_t> head;
    putBytes(head, fourcc, 4);
    put<uint32_t>(head, 0);
    put<uint64_t>(head, payload.size());
    std::vector<uint8_t> tail;
    put<uint32_t>(tail, detail::crc32c(payload.data(), payload.size()));
    if (std::fwrite(head.data(), 1, head.size(), f_) != head.size() ||
        std::fwrite(payload.data(), 1, payload.size(), f_) != payload.size() ||
        std::fwrite(tail.data(), 1, tail.size(), f_) != tail.size())
        throw std::runtime_error("craw::Writer: write failed");
    pos_ += head.size() + payload.size() + tail.size();
    index_.push_back(e);
}

void Writer::addFrame(const uint16_t* raw, size_t stride, const FrameDesc& desc,
                      int64_t ts, std::string_view frameMetaJson) {
    encodeFrame(raw, stride, desc, codec_, threads_);
    scratch_.clear();
    put<uint64_t>(scratch_, frames_);
    put<int64_t>(scratch_, ts);
    put<uint32_t>(scratch_, static_cast<uint32_t>(frameMetaJson.size()));
    putBytes(scratch_, frameMetaJson.data(), frameMetaJson.size());
    putBytes(scratch_, codec_.data(), codec_.size());
    putChunk("FRAM", ts, scratch_);
    ++frames_;
}

void Writer::addAudio(int64_t ts, AudioFormat fmt, uint8_t channels, uint32_t rate,
                      const void* samples, uint32_t frameCount) {
    const size_t bps = fmt == AudioFormat::S16 ? 2 : fmt == AudioFormat::S24 ? 3 : 4;
    scratch_.clear();
    put<int64_t>(scratch_, ts);
    put<uint8_t>(scratch_, static_cast<uint8_t>(fmt));
    put<uint8_t>(scratch_, channels);
    put<uint16_t>(scratch_, 0);
    put<uint32_t>(scratch_, rate);
    put<uint32_t>(scratch_, frameCount);
    putBytes(scratch_, samples, bps * channels * frameCount);
    putChunk("AUDI", ts, scratch_);
}

void Writer::addImu(ImuSensor sensor, const ImuSample* s, uint32_t count) {
    scratch_.clear();
    put<uint8_t>(scratch_, static_cast<uint8_t>(sensor));
    put<uint8_t>(scratch_, 0);
    put<uint16_t>(scratch_, 0);
    put<uint32_t>(scratch_, count);
    for (uint32_t i = 0; i < count; ++i) {
        put<int64_t>(scratch_, s[i].timestampNs);
        putF32(scratch_, s[i].x);
        putF32(scratch_, s[i].y);
        putF32(scratch_, s[i].z);
    }
    putChunk("IMU_", count ? s[0].timestampNs : 0, scratch_);
}

void Writer::addShading(int64_t ts, uint32_t id, uint16_t w, uint16_t h, const float* gains) {
    scratch_.clear();
    put<int64_t>(scratch_, ts);
    put<uint32_t>(scratch_, id);
    put<uint16_t>(scratch_, w);
    put<uint16_t>(scratch_, h);
    for (size_t i = 0; i < size_t{4} * w * h; ++i) putF32(scratch_, gains[i]);
    putChunk("LSHD", ts, scratch_);
}

void Writer::close() {
    if (!f_) return;
    const uint64_t indexOffset = pos_;
    scratch_.clear();
    put<uint32_t>(scratch_, static_cast<uint32_t>(index_.size()));
    for (auto& e : index_) {
        putBytes(scratch_, e.fourcc, 4);
        put<uint32_t>(scratch_, 0);
        put<uint64_t>(scratch_, e.offset);
        put<int64_t>(scratch_, e.ts);
    }
    putChunk("INDX", 0, scratch_);
    std::vector<uint8_t> trailer;
    put<uint64_t>(trailer, indexOffset);
    putBytes(trailer, "CRAWEND\0", 8);
    const bool ok = std::fwrite(trailer.data(), 1, trailer.size(), f_) == trailer.size();
    const bool closed = std::fclose(f_) == 0;
    f_ = nullptr;
    if (!ok || !closed) throw std::runtime_error("craw::Writer: close failed");
}

}  // namespace craw
