# Next release — draft changelist

Unreleased candidate; version and date will be assigned during release preparation.
These notes describe local work awaiting source staging, fresh platform CI and
package verification. No new binaries have been published with this document.

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
- Batch ISO/BIN resource extraction into32-sector reads, preserving raw-sector
  handling, progress and cancellation. CD audio was already read in chunks.
- Host reader benchmarks improved; SD-card/full-level-load gains still need
  device measurement. Loose-file reading is unchanged.

## Fixes and diagnostics

- Fix Touch and Destroy leaving a turret/tank base visible but noncolliding and
  impossible to damage. Previously affected bases already stored in saves are
  not retroactively repaired.
- Add a Linux RSS/PSS and memory-category breakdown beneath the debug FPS display.
- Add an optional FPS/RSS counter in Advanced video settings, disabled by default.

## Compatibility and validation

- Save schema remains v35 with the existing optional lossless zlib envelope.
  Supported older raw saves still load. Builds predating compressed-save support
  cannot read compressed checkpoints; old large saves can still have load peaks.
- Local suite:154/154 tests passed, plus targeted resource, rendering and
  save-continuation audits. Hardware/platform release validation remains pending.
- Native-resolution rendering, immediate-mode desktop submission, short resident
  sound effects and bounded upload scratch remain unchanged after review.
- PortMaster ARM64 / GLES support has its own CI workflow and Alpha Preview
  packages. Desktop tags (`v*`) and handheld tags (`portmaster-v*`) build and
  prepare their releases independently. Both release jobs create drafts.
- Fresh ARM64 CI validation is still pending; local host GLES checks do not
  establish device performance or long-session stability.
