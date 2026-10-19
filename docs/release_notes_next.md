# Vurus v0.1.3 and PortMaster Alpha Preview

Downloads: [desktop v0.1.3](https://github.com/rzrbld/vurus/releases/tag/v0.1.3)
and [PortMaster Alpha Preview](https://github.com/rzrbld/vurus/releases/tag/portmaster-v0.1.0-alpha.1).
These releases have independent build and publication workflows. Packages are
built in GitHub CI and include SHA-256 checksums.

## Lower memory use

- Keep the current level and its restart state instead of accumulating completed
  worlds in memory and new saves. Supported legacy saves remain readable.
- Share unchanged object properties between live state and snapshots.
- Reuse fully released generated projectile/effect records while preserving
  persistent scenery, enemies and original creation/animation ordering.
- Stream CD music and the original AVI intro through bounded buffers; add a
  512 KiB video read-ahead queue for buffered playback.
- Store decoded sprites and terrain in their original compact 16-bit format;
  avoid unnecessary full-bank pixel decoding for static UI and known sequences.
- Reduce sprite-filter construction temporaries and avoid GPU readback for world
  sprites with available CPU source pixels. Preserve filtering and color-key edges.
- Account for actual uploaded sprite texture payload in the residency cache.

Observed ordinary gameplay RSS fell from roughly 500–700 MB to 170–250 MB in the
user's sessions. This is not a matched cross-platform benchmark or a RAM guarantee.
See [measurements and limitations](performance_update_2026-10-19.md).

## Resource loading

- Keep one synchronized archive handle instead of reopening it for every entry.
- Read only archive headers/directories during initialization instead of loading
  whole archives temporarily. No whole-file mapping or resource cache added.
- Batch ISO/BIN resource extraction into 32-sector reads, preserving raw-sector
  handling, progress and cancellation. CD audio was already read in chunks.
- Host reader benchmarks improved; SD-card/full-level-load gains still need
  device measurement. Loose-file reading is unchanged.

## Fixes and diagnostics

- Fix Touch and Destroy leaving a turret/tank base visible but noncolliding and
  impossible to damage. Previously affected bases already stored in saves are
  not retroactively repaired.
- Add a Linux RSS/PSS and memory-category breakdown beneath the debug FPS display.
- Add an optional FPS/RSS counter in Advanced video settings, disabled by default.
- Correct the Windows retry-save ownership signature; close retained stream
  owners before test-fixture cleanup on Windows.

## Compatibility and validation

- Save schema remains v35 with the existing optional lossless zlib envelope.
  Supported older raw saves still load. Builds predating compressed-save support
  cannot read compressed checkpoints; old large saves can still have load peaks.
- CI: Linux passed 154 tests; Windows and each Mac architecture passed 150;
  ARM64 GLES passed 152 plus its renderer check. Targeted resource, rendering
  and save-continuation audits complement these automated tests.
- Native-resolution rendering, immediate-mode desktop submission, short resident
  sound effects and bounded upload scratch remain unchanged after review.
- PortMaster ARM64 / GLES support has its own CI workflow and Alpha Preview
  packages. Desktop tags (`v*`) and handheld tags (`portmaster-v*`) build and
  prepare their releases independently. Both release jobs create drafts.
- Physical hardware, long-session and repeated-transition testing continues;
  automated tests do not establish full device acceptance.
