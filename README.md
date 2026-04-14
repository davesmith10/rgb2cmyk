# rgb2cmyk

A command-line tool that converts sRGB JPEG images to CMYK JPEG using ICC colour profiles. Designed for print-on-demand workflows where accurate colour space conversion is required.

## Dependencies

- [Little CMS 2](https://www.littlecms.com/) (`lcms2`) — ICC colour profile transforms
- [libjpeg-turbo](https://libjpeg-turbo.org/) — JPEG decode and encode

On Oracle Linux / RHEL / Fedora:

```bash
sudo dnf install lcms2-devel libjpeg-turbo-devel
```

On Debian / Ubuntu:

```bash
sudo apt install liblcms2-dev libjpeg-turbo8-dev
```

## Build

```bash
cmake -S . -B build
cmake --build build
```

The binary is produced at `build/rgbtocmyk`.

## Usage

```
rgbtocmyk --strip-profile --sRGB <profile.icc> --CMYK <profile.icc> [--quality <1-100>] <input.jpg> <output.jpg>
```

| Option | Description |
|---|---|
| `--strip-profile` | Strip the embedded ICC profile from the input before processing |
| `--sRGB <path>` | Source sRGB ICC profile to assign to the input image |
| `--CMYK <path>` | Destination CMYK ICC profile to convert into |
| `--quality <1-100>` | Output JPEG quality (default: preserve input quality) |

### Example

```bash
./build/rgbtocmyk \
  --strip-profile \
  --sRGB profiles/sRGB_v4_ICC_preference.icc \
  --CMYK profiles/SWOP2006_Coated3v2.icc \
  cover.jpg cover-cmyk.jpg
```

After conversion, the tool prints a verification summary modelled on `magick identify -verbose`:

```
Image: cover-cmyk.jpg
  Format: JPEG (Joint Photographic Experts Group JFIF format)
  Geometry: 2048x2048+0+0
  Colorspace: CMYK
  Type: ColorSeparation
  Depth: 8-bit
  Channels: 4.0
  Channel statistics:
    Pixels: 4194304
    Cyan:
      min: 0  (0)
      max: 255  (1)
      mean: 131.2  (0.5145846)
      ...
  Total ink density: 300.784%
  Profiles:
    Profile-icc: 2747952 bytes
      icc:description: SWOP2006_Coated3v2.icc
  Filesize: 12.0187MiB
```

## ICC Profiles

ICC profiles are not bundled. Recommended sources:

- **sRGB v4**: [ICC sRGB profiles](https://www.color.org/srgbprofiles.xalter) — use `sRGB_v4_ICC_preference.icc`
- **SWOP / PSO Coated**: [ICC profile registry](https://www.color.org/registry/index.xalter)

## Licence

MIT
