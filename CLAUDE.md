# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Goal

`rgbtocmyk` — a C++ CLI tool that replicates ImageMagick's three-step ICC profile conversion for sRGB JPEG → CMYK JPEG. Target use case: print-on-demand book covers, files up to 25 MB.

## Reference Workflow (Ground Truth)

```bash
magick input.jpg +profile icm \
  -profile profiles/sRGB_v4_ICC_preference.icc \
  -profile profiles/SWOP2006_Coated3v2.icc \
  output.jpg
```

Three semantic steps:
1. **Strip** the embedded ICC profile from the input JPEG
2. **Assign** the sRGB source profile (no pixel change — pure tagging)
3. **Transform** pixel values into the CMYK destination color space

## CLI Design

GNU/POSIX conventions. All profile arguments are explicit and required.

```
rgbtocmyk --strip-profile --sRGB <profile.icc> --CMYK <profile.icc> [--quality <1-100>] <input.jpg> <output.jpg>
```

- `--strip-profile` — strip the embedded ICC profile before processing
- `--sRGB <path>` — source sRGB ICC profile to assign
- `--CMYK <path>` — destination CMYK ICC profile to convert into
- `--quality <n>` — output JPEG quality (default: same as input)
- Positional args: input path, output path (output is required; no default derivation)

## Libraries

- **Little CMS 2 (lcms2)** — ICC profile loading and color transforms
- **libjpeg-turbo** — JPEG decode and encode
- Install on Oracle Linux 9.5: `sudo dnf install lcms2-devel libjpeg-turbo-devel`

## Build System

CMake, targeting a local binary (no install target).

```bash
cmake -S . -B build
cmake --build build
./build/rgbtocmyk --help
```

## Verification Output

Modeled on `magick identify -verbose`. After conversion, print to stdout:
- Filename, format, geometry, colorspace, type, bit depth, channel count
- Per-channel statistics: min, max, mean, median, standard deviation
- Total ink density (CMYK output only)
- Embedded profile info (description, size in bytes)
- File size

## Test Data & Profiles

- `test-data/` — reference input/output pairs (`2048px-100.jpg` / `2048px-100-CMYK.jpg`, etc.)
- `profiles/` — `sRGB_v4_ICC_preference.icc`, `SWOP2006_Coated3v2.icc`, `PSOcoated_v3.icc`

Cross-check output with:
```bash
magick identify -verbose <output.jpg>
```
