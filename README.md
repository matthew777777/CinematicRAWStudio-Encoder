# Cinematic RAW Studio — Encoder

C++20 lossless encoder and streaming writer for `.CRAW` (spec: `../CinematicRAWStudio-Spec/SPEC.md`).

```
cmake -S . -B build -G Ninja && ninja -C build
./build/craw_enc_conformance          # byte-exact vs reference vectors
./build/craw_bench                    # 4080x3060 10-bit throughput, JSON
scripts/build_android.sh              # Release arm64-v8a (NDK)
```

API: `craw::encodeFrame()` (include/craw/Encoder.h), `craw::Writer` (include/craw/Writer.h).
License: Apache-2.0.
