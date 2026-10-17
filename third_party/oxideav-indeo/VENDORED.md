Source: https://github.com/OxideAV/oxideav-indeo
Revision: b11d38bf2bf407677a2d19dfebd0a678ecdd8208
License: MIT, retained in LICENSE. Copyright 2026 Karpelès Lab Inc.

Copied upstream src/, Cargo.toml and README.md. The port compiles only the
Indeo5 module through its standalone Rust staticlib bridge. Two cfg gates in
src/indeo5/mod.rs exclude framework registration and its reexports. The custom-size GOP field in src/indeo5/gop.rs is corrected to read height
from the low13bits and width from the high13bits; its unit test is corrected too.
Upstream reversed them, reading the original320x180 movie as180x320.
Confirmed against AVI metadata and FFmpeg's independent decoder header reader:
https://raw.githubusercontent.com/FFmpeg/FFmpeg/master/libavcodec/indeo5.c
(decode_gop_header, custom picture size). No FFmpeg code is incorporated.
All other codec algorithms are unchanged. No oxideav-core or third-party Rust crates are needed
by this standalone build; the original upstream Cargo.toml is retained as
provenance, not used for the port build. Rust's standard library is statically
linked (Rust toolchain Apache-2.0/MIT licensing applies).

Rust standard-library license notices for the build toolchain are retained in
rust-licenses/ (COPYRIGHT-library.html and referenced license texts). The bridge
contains no Cargo dependencies. Regenerate notices when changing toolchain.
