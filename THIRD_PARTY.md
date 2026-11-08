# Source and license notices

Note4C Voice is distributed under GPL-3.0-only; see `LICENSE`.

The native SSD2683 display and board power support originate in
[LitoMore/trmnl-firmware](https://github.com/LitoMore/trmnl-firmware),
itself a fork of [usetrmnl/trmnl-firmware](https://github.com/usetrmnl/trmnl-firmware).
The inspected `origin/zectrix` commit is
`76f89b09dec420c73a970a7c5bb2fcc61009eba1`:

- `src/display.cpp`: command/refresh sequence, packed pixels and SPI3 bus;
  adapted in `src/epd.cpp` and `src/panel.cpp`.
- `src/zectrix.cpp`: battery latch and RTC GPIO holds; adapted in `src/board.cpp`.
- `src/DEV_Config.h`, `boards/esp32s3_n16r8.json`, `docs/zectrix.md`: pin mapping,
  memory and upload configuration.

Original copyright remains with the TRMNL firmware authors and contributors.
Their GPL version 3 license is retained in `LICENSE`.

The ES8311 driver in `vendor/es8311/` is copied unmodified from
[Espressif esp-bsp](https://github.com/espressif/esp-bsp/tree/73ee07b1ad56f865f13a2739be8bb6808c1da58a/components/es8311),
commit `73ee07b1ad56f865f13a2739be8bb6808c1da58a`. Its Apache-2.0 license and
copyright headers are retained. `src/es8311_vendor.c` integrates it into
the PlatformIO build. Audio wiring was checked against the
[official Note4C documentation](https://wiki.zectrix.com/en/hardware/note4c/quick-start)
and the shared board's [reference implementation](https://github.com/itopinion/zectrix-note4-epd-demo/blob/main/components/zectrix_board/zectrix_board.cc).
No complete monochrome Note4 firmware is incorporated.

The SSD2683 `0x83` window packet format and X alignment were checked against
`SetPartialWindow` in the MIT-licensed
[Zectrix Note4 reference](https://github.com/itopinion/zectrix-note4-epd-demo/blob/main/components/zectrix_epd/zectrix_epd.cc).
Its monochrome waveform/LUT and transition encoding are not used. Native
color OTP window operation was tested by the user in `experiments/partial-lines`
on 11 October 2026, then integrated in `src/panel.cpp`.
The DOWN button (GPIO18) was checked against the same shared board's
[hardware map](https://github.com/itopinion/zectrix-note4-epd-demo/blob/main/docs/HARDWARE.md).

Noto Sans Regular and SemiBold in `assets/` are from
[notofonts/noto-fonts](https://github.com/notofonts/noto-fonts/tree/ffebf8c1ee449e544955a7e813c54f9b73848eac),
commit `ffebf8c1ee449e544955a7e813c54f9b73848eac`. They are licensed under
SIL Open Font License 1.1, retained in `assets/OFL.txt`. Generated glyph
bitmaps in `src/fonts.cpp` derive from those fonts and retain that license.

Exact download URLs and SHA256 hashes of vendored files are recorded in
`vendor/SOURCES.json`. `tools/check.py` verifies their integrity.

Arduino-ESP32 2.0.17, its bundled libraries, and ESP-IDF retain their
respective licenses in the PlatformIO framework package. The bundled cJSON
parser is linked from ESP-IDF. PlatformIO, esptool and Pillow are build tools.

The cue WAVs in `assets/sounds/` are generated for this project by
`tools/generate_sounds.py` and are distributed under GPL-3.0-only.
They contain no third-party recordings.
