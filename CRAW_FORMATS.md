# CRAW formats and compression modes

CRAW provides four ways to compress the same RAW sensor data inside a `.CRAW` file. They differ in how they describe those numbers efficiently. All four reconstruct the original samples exactly.

## What is being compressed?

A Bayer camera sensor records a mosaic of red, green, and blue measurements. Each sensor position measures one colour; software later reconstructs a full-colour image through demosaicing.

CRAW preserves those original measurements. Compression itself does not bake in white balance, a display look, or demosaicing.

The basic compression idea is **prediction**. Nearby measurements often have similar values, so storing their differences can take fewer bits:

```text
Actual sample:      2037
Predicted sample:   2034
Stored difference:   +3

Decoder reconstructs: 2034 + 3 = 2037
```

Even a poor prediction preserves the sample exactly—it simply produces a larger difference to store.

## Overview

| Format | Purpose | Main tradeoff |
|---|---|---|
| CRAW-Fast — codec 1 | Compact RAW encoding using prediction and adaptive Rice coding. | Emphasizes compression more than the original Live mode. |
| CRAW-Live — codec 2 | Fast compression for continuous camera recording. | Prioritizes simple block packing over the smallest files. |
| CRAW-Live v2 — codec 3 | Live compression with improved handling of outlier differences. | Actual speed and size depend on footage and hardware. |
| Desktop Compact — codec 4 | Recompressing recordings into smaller archives. | Substantially slower encoding than Live v2 because it searches more options. |

## 1. CRAW-Fast — codec 1

This combines spatial prediction with compression of relationships between the Bayer colour planes.

The encoder divides a frame into tiles and separates the Bayer samples into four planes. It represents three planes as differences from a reference plane, which can reduce repeated information between channels.

It then:

1. Predicts samples from neighbouring values using an edge-aware predictor called **MED**.
2. Converts signed prediction differences into unsigned numbers.
3. Packs those numbers using **adaptive Rice coding**, which represents small differences with short bit sequences.

MED uses the left, upper, and upper-left neighbours to follow local image structure. Rice coding then exploits the fact that many prediction errors are small.

This mode puts more work into compact representation than the original Live mode. “Fast” is its name, rather than a guarantee that it is the fastest available codec.

## 2. CRAW-Live — codec 2

This favours simple, regular operations suitable for recording continuously.

Instead of using variable-length Rice codes for each difference, it groups prediction differences into small blocks and chooses a bit width that fits the block.

For example:

```text
16 differences all fit within 5 bits
→ store each difference using 5 bits
→ 80 bits of sample data, plus a small block header
```

That is efficient for processors because packing fixed-width values is straightforward.

The weakness is **outliers**: if one difference needs many bits, the block must accommodate it, even when the other differences are small. Compression can suffer around noise, sharp detail, or isolated unusual values.

## 3. CRAW-Live v2 — codec 3

This retains the fast block-packing approach but adds a way to handle outliers.

The implementation predicts samples within each Bayer plane using neighbouring samples. It can encode a block either with ordinary fixed-width packing or with **patched frame-of-reference encoding**, usually shortened to PFOR.

PFOR lets most values use a small bit width while storing exceptional values separately:

```text
Most differences need 4 bits
One difference needs 11 bits

Ordinary packing: allocate 11 bits to every value
PFOR: use a narrow base representation and extra data for exceptions
```

The encoder compares representations and chooses how to encode the block.

This addresses a specific inefficiency in Live v1. It does not guarantee smaller files or greater speed for every scene; that depends on the distribution of sample differences and the hardware.

## 4. Desktop Compact — codec 4

This spends more encoding time searching for an efficient representation.

For each tile, it tries four predictors:

- An average of four neighbouring samples.
- The edge-aware MED predictor.
- An average of the left and upper samples.
- A weighted combination of left, upper, and upper-right samples.

For each candidate, it calculates prediction differences and searches Rice coding parameters for blocks of up to 16 samples. It chooses the predictor with the lowest calculated coding cost and writes the selected parameters into the file.

That repeated search is why encoding is substantially slower than Live v2. The decoder can read the chosen settings directly; it does not need to repeat the search.

Desktop Compact is intended for recompressing recordings afterward, when storage savings matter more than keeping up with capture.

## What stays the same across all four?

| Property | Behaviour |
|---|---|
| RAW sample values | Preserved exactly. |
| Image quality after identical processing | Identical. |
| Compression between frames | These implementations compress each frame independently. |
| File size | Depends on scene detail, noise, and codec. |
| Difficult-to-compress tiles | Can fall back to storing uncompressed samples. |

Noise matters especially: random sensor noise is difficult to predict, so noisy footage generally compresses less efficiently. Lossless compression preserves that noise too.

## The `.CRAW` container and CinemaDNG

These are a different level of the workflow from the four compression modes.

- **`.CRAW`** is the recording container. It holds compressed frames, audio, timestamps, metadata, and optional sensor data. Codecs 1–4 describe how its frame data is compressed.
- **CinemaDNG** is an export format that stores RAW frames as a sequence of DNG image files, with sound exported separately in this workflow.

For recording, use a Live mode. For archiving afterward, Desktop Compact is designed to trade additional encoding time for storage savings. Compression ratios and throughput should be measured on representative footage and hardware.

## Sources

- [Encoder implementation](src/Encoder.cpp)
- [Project README](README.md)
