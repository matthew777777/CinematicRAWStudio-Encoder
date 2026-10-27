# Cinematic RAW Studio — Encoder

C++20 lossless encoder and streaming container writer for `.CRAW`
(spec: `../CinematicRAWStudio-Spec/SPEC.md`). Apache-2.0. Used by the RawLens
Android app through JNI (vendored with `tools/sync_craw.sh` in RawLens).

## Build

```
cmake -S . -B build -G Ninja && ninja -C build
./build/craw_enc_conformance          # byte-exact against the Python reference vectors
./build/craw_bench --bits 12          # 4080x3060 throughput, JSON
scripts/build_android.sh              # Release arm64-v8a with the newest NDK
```
End users normally install everything with
`../CinematicRAWStudio-Spec/scripts/install-macos.sh`.

## API

```cpp
#include <craw/Writer.h>

craw::Writer w("take.craw", metaJson);               // META chunk (SPEC.md §2)
craw::FrameDesc d{4080, 3060, 12, craw::Cfa::RGGB};  // width, height, bit depth, CFA
w.addFrame(raw16, strideSamples, d, timestampNs, frameJson);
w.addAudio(ts, craw::AudioFormat::F32, 2, 48000, samples, frames);
w.addImu(craw::ImuSensor::Gyroscope, samples, count);
w.addShading(ts, id, columns, rows, gains);          // frames reference it with "lshd_ref"
w.close();                                           // INDX + trailer
```

Encode on worker threads and commit in order (what RawLens does):
```cpp
size_t n = craw::encodeFrame(raw16, stride, d, dst, capacity);  // any thread, into your buffer
w.addEncodedFrame(dst, n, timestampNs, frameJson);              // committer thread, no copy
```
`encodeFrame(..., threads)` also splits one frame over tiles. A sample above
`2^bitDepth` throws `std::invalid_argument` (RawLens retries at 16 bits).

## Tools

| tool | usage |
|------|-------|
| `craw_enc` | `craw_enc out.craw --width W --height H --bits B [--cfa RGGB] [--fps 30] [--meta meta.json] f0.raw f1.raw …` (uint16 LE frames; `fN.json` sidecars become frame JSON) |
| `craw_bench` | `craw_bench [--input frame.raw --width W --height H] [--bits 12] [--frames 30] [--threads N] [--tile 128]` |

## Performance

ARM NEON for plane extraction, MED residuals and block sums; four Rice code
words per 32-bit write; parallel frame CRC (CRC32C combine); zero-copy writes.
MT6878 phone, 4080x3060 12-bit: ~650 MPix/s with 8 threads; through the
RawLens JNI path with storage, 653–700 MPix/s (52–56 fps).

## License

Apache-2.0. See LICENSE and NOTICE.
