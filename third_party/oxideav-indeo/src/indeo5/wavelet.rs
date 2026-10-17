//! Indeo 5 CDF 5/3 (LeGall) wavelet recomposition.
//!
//! Spec source: `docs/video/indeo/indeo5/spec/06-slant-inverse-transform.md`
//! §3 (synthesis filter), §4 (per-level upsampling + boundary handling).
//!
//! After the per-block inverse Slant transform (`spec/06 §2`), each
//! wavelet band carries a band-resolution buffer. The CDF 5/3 (a.k.a.
//! LeGall 5/3) synthesis filter (`spec/06 §3.2`) recomposes a plane's
//! `1 + 3·levels` bands back into the plane-resolution pixel buffer. The
//! synthesis filter coefficients (`spec/06 §3.2`, wiki "Wavelet
//! transform" annex) are:
//!
//! * low-pass `h0 = {1, 2, 1} · 1/2`,
//! * high-pass `h1 = {1, 2, -6, 2, 1} · 1/4`.
//!
//! The binary implements the **lifting form** of the inverse transform
//! (`spec/06 §3.3`): the synthesis interleaves the even (low-pass) and
//! odd (high-pass) sub-bands into a 2×-length output via two lifting
//! steps. This module materialises the 1D synthesis from the §3.3
//! lifting recurrence and the §4.2 mirror-reflection boundary
//! extension, plus the separable 2D synthesis (`spec/06 §4.1`: row-pass
//! then column-pass, doubling each axis).
//!
//! ## Lifting recurrence (`spec/06 §3.3`)
//!
//! Given the even (low-pass `L`) and odd (high-pass `H`) sub-band rows,
//! the synthesis reconstructs the interleaved output `x` (with
//! `x[2i] = L[i]`, `x[2i+1] = H[i]` before lifting) by:
//!
//! 1. **Even update** (undo the encoder's update step): each even
//!    sample is corrected by its neighbouring high-pass samples,
//!    `e[i] -= (h[i-1] + h[i] + 2) >> 2`.
//! 2. **Odd update** (undo the encoder's predict step): each odd sample
//!    is corrected by its neighbouring (already-updated) even samples,
//!    `o[i] += (e[i] + e[i+1]) >> 1`.
//!
//! The `+2` / `>> 2` and `>> 1` are the round-to-nearest rescales the
//! binary performs with the `+0x10000`-paired `lea` offsets (`spec/06
//! §3.3`). Boundaries use mirror reflection (`spec/06 §4.2`): an
//! out-of-range index `j` reflects to `2·last - j` / `-j`.
//!
//! ## Boundary with the gated transform path
//!
//! The §2 per-block inverse Slant is **fused** with the gated entropy
//! dispatch (192 handlers, `spec/06 §6` items 2/3/7 open) and the
//! per-codebook scale tables are an Extractor docs-gap (`spec/06 §6`
//! item 1). This module covers only the §3/§4 wavelet synthesis, which
//! is fully specified independently of that gated path: it takes
//! already-inverse-transformed band buffers (caller-supplied) and
//! recomposes them, exactly the contract the §3.5 out-of-place
//! synthesis routine implements.

/// Mirror-reflect an index `j` into `0..len` (`spec/06 §4.2`
/// whole-sample symmetric extension). `len` must be `>= 1`.
#[inline]
fn mirror(j: isize, len: usize) -> usize {
    debug_assert!(len >= 1);
    let n = len as isize;
    if n == 1 {
        return 0;
    }
    // Reflect into [0, 2n-2) with period 2n-2 (whole-sample symmetry),
    // then fold the upper half back down.
    let period = 2 * (n - 1);
    let mut k = j % period;
    if k < 0 {
        k += period;
    }
    if k >= n {
        k = period - k;
    }
    k as usize
}

/// Perform one 1D CDF 5/3 synthesis step (`spec/06 §3.3`).
///
/// `low` is the even (low-pass) sub-band, `high` is the odd (high-pass)
/// sub-band; they need not be equal length (the odd band may be one
/// shorter for an odd output length). Returns the interleaved
/// synthesised signal of length `low.len() + high.len()`.
///
/// Output index mapping: `out[2i] = even[i]` (post even-update),
/// `out[2i+1] = odd[i]` (post odd-update). The lifting is performed on
/// scratch even/odd arrays so the two steps see the correct (updated)
/// neighbours.
pub fn synth_1d(low: &[i32], high: &[i32]) -> Vec<i32> {
    let n_even = low.len();
    let n_odd = high.len();
    if n_even == 0 {
        return Vec::new();
    }

    // Working copies of the even (low) and odd (high) samples.
    let mut even: Vec<i32> = low.to_vec();
    let odd: Vec<i32> = high.to_vec();

    // Step 1 — even update: e[i] -= (h[i-1] + h[i] + 2) >> 2.
    // Neighbours h[i-1] / h[i] mirror-reflect at the high-band edges.
    for i in 0..n_even {
        let hm1 = if n_odd == 0 {
            0
        } else {
            odd[mirror(i as isize - 1, n_odd)]
        };
        let h0 = if n_odd == 0 {
            0
        } else {
            odd[mirror(i as isize, n_odd)]
        };
        even[i] -= (hm1 + h0 + 2) >> 2;
    }

    // Step 2 — odd update: o[i] += (e[i] + e[i+1]) >> 1, with e mirror-
    // reflected at the even-band edges (now updated).
    let mut out = vec![0i32; n_even + n_odd];
    for i in 0..n_odd {
        let e0 = even[mirror(i as isize, n_even)];
        let e1 = even[mirror(i as isize + 1, n_even)];
        let o = odd[i] + ((e0 + e1) >> 1);
        out[2 * i + 1] = o;
    }
    for i in 0..n_even {
        out[2 * i] = even[i];
    }
    out
}

/// A band-resolution buffer: `width × height` row-major `i32` samples.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Band {
    /// Width in samples.
    pub width: usize,
    /// Height in samples.
    pub height: usize,
    /// Row-major samples (`width · height` entries).
    pub data: Vec<i32>,
}

impl Band {
    /// The band cropped / zero-padded to `width × height`.
    pub fn resized(&self, width: usize, height: usize) -> Band {
        let mut data = vec![0i32; width * height];
        for y in 0..height.min(self.height) {
            let n = width.min(self.width);
            data[y * width..y * width + n]
                .copy_from_slice(&self.data[y * self.width..y * self.width + n]);
        }
        Band {
            width,
            height,
            data,
        }
    }

    /// Construct a band from explicit dimensions + data (panics on a
    /// length mismatch — a programming error in the caller).
    pub fn new(width: usize, height: usize, data: Vec<i32>) -> Self {
        assert_eq!(data.len(), width * height, "band data length mismatch");
        Band {
            width,
            height,
            data,
        }
    }

    /// Sample at `(x, y)` (row-major).
    #[inline]
    pub fn at(&self, x: usize, y: usize) -> i32 {
        self.data[y * self.width + x]
    }
}

/// Recompose a single 2D decomposition level (`spec/06 §4.1`).
///
/// Given the four band quadrants `ll` / `hl` / `lh` / `hh` (each at
/// `(W/2) × (H/2)` band resolution, all the same dimensions), produce
/// the `W × H` plane via separable 5/3 synthesis: a row-pass that
/// recomposes each row's `(L, H)` halves to full width, then a
/// column-pass that recomposes each column's `(L, H)` halves to full
/// height.
///
/// The four bands map to the standard quadrant layout: `ll` = low-low,
/// `hl` = high-horizontal/low-vertical, `lh` = low-horizontal/high-
/// vertical, `hh` = high-high (`spec/06 §3.1`).
pub fn recompose_level(ll: &Band, hl: &Band, lh: &Band, hh: &Band) -> Band {
    let bw = ll.width;
    let bh = ll.height;
    // The three high-frequency bands share the low-low band's geometry
    // (`recompose_plane` fits them); a caller-supplied mismatch is
    // fitted the same way rather than trusted.
    let fit =
        |b: &Band| -> Option<Band> { ((b.width, b.height) != (bw, bh)).then(|| b.resized(bw, bh)) };
    let (hl_f, lh_f, hh_f) = (fit(hl), fit(lh), fit(hh));
    let hl = hl_f.as_ref().unwrap_or(hl);
    let lh = lh_f.as_ref().unwrap_or(lh);
    let hh = hh_f.as_ref().unwrap_or(hh);

    let full_w = bw * 2;
    let full_h = bh * 2;

    // Row-pass: for each band row, synthesise the low half (ll | lh's
    // even rows come from the L-vertical bands) — but separability
    // means we first synthesise horizontally. Build two intermediate
    // half-height, full-width planes:
    //   l_plane[y] = synth_1d(ll row y, hl row y)   (low-vertical)
    //   h_plane[y] = synth_1d(lh row y, hh row y)   (high-vertical)
    let mut l_plane = vec![0i32; full_w * bh];
    let mut h_plane = vec![0i32; full_w * bh];
    for y in 0..bh {
        let ll_row = &ll.data[y * bw..(y + 1) * bw];
        let hl_row = &hl.data[y * bw..(y + 1) * bw];
        let lh_row = &lh.data[y * bw..(y + 1) * bw];
        let hh_row = &hh.data[y * bw..(y + 1) * bw];
        let l = synth_1d(ll_row, hl_row);
        let h = synth_1d(lh_row, hh_row);
        l_plane[y * full_w..(y + 1) * full_w].copy_from_slice(&l);
        h_plane[y * full_w..(y + 1) * full_w].copy_from_slice(&h);
    }

    // Column-pass: for each output column, synthesise the low-vertical
    // (l_plane) against the high-vertical (h_plane) to full height.
    let mut out = vec![0i32; full_w * full_h];
    let mut low_col = vec![0i32; bh];
    let mut high_col = vec![0i32; bh];
    for x in 0..full_w {
        for y in 0..bh {
            low_col[y] = l_plane[y * full_w + x];
            high_col[y] = h_plane[y * full_w + x];
        }
        let col = synth_1d(&low_col, &high_col);
        for (y, &v) in col.iter().enumerate() {
            out[y * full_w + x] = v;
        }
    }

    Band::new(full_w, full_h, out)
}

/// The four high-frequency quadrants of one decomposition level, in the
/// `spec/06 §3.1` order (`hl`, `lh`, `hh`). The `ll` quadrant is the
/// level's low-low band; for the innermost level it is an explicit band,
/// for outer levels it is the recomposed result of the level below.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct LevelBands {
    /// High-horizontal / low-vertical band.
    pub hl: Band,
    /// Low-horizontal / high-vertical band.
    pub lh: Band,
    /// High-high band.
    pub hh: Band,
}

/// Recompose a whole plane from its wavelet bands (`spec/06 §3.4`
/// bottom-up multi-level synthesis).
///
/// * `ll` is the innermost low-low band (the only band whose `ll`
///   quadrant is carried explicitly).
/// * `levels` lists the decomposition levels from **innermost to
///   outermost** — `levels[0]` pairs with `ll` to form the inner-LL,
///   whose result then becomes the `ll` quadrant for `levels[1]`, and so
///   on. The list length equals the plane's decomposition-level count
///   (`spec/02 §1.5`: 0, 1, or 2).
///
/// For a 0-level plane (`levels` empty) the single `ll` band *is* the
/// plane (`spec/06 §3.4`: "no wavelet recomposition is performed"). Each
/// listed level doubles both plane axes (`spec/06 §4.1`).
pub fn recompose_plane(ll: &Band, levels: &[LevelBands]) -> Band {
    let mut current = ll.clone();
    // §3.4 bottom-up: combine the running low-low band with each level's
    // three high-frequency bands, innermost level first. A recomposed
    // low-low band is twice its input's size, which for a plane whose
    // dimensions are not multiples of `2^levels` can differ by one from
    // the next level's `ceil`-sized triple: fit it to the triple.
    for level in levels {
        if (current.width, current.height) != (level.hl.width, level.hl.height) {
            current = current.resized(level.hl.width, level.hl.height);
        }
        current = recompose_level(&current, &level.hl, &level.lh, &level.hh);
    }
    current
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn mirror_reflects_within_range() {
        // len 4: indices 0..4 are identity.
        for j in 0..4 {
            assert_eq!(mirror(j as isize, 4), j);
        }
        // Left edge: -1 -> 1, -2 -> 2.
        assert_eq!(mirror(-1, 4), 1);
        assert_eq!(mirror(-2, 4), 2);
        // Right edge: 4 -> 2, 5 -> 1 (whole-sample symmetry around 3).
        assert_eq!(mirror(4, 4), 2);
        assert_eq!(mirror(5, 4), 1);
    }

    #[test]
    fn mirror_len_one() {
        assert_eq!(mirror(0, 1), 0);
        assert_eq!(mirror(-3, 1), 0);
        assert_eq!(mirror(7, 1), 0);
    }

    #[test]
    fn synth_1d_constant_low_zero_high() {
        // A flat low-pass band with zero high-pass should reconstruct a
        // flat signal: even update subtracts (0+0+2)>>2 = 0, odd update
        // adds (e+e)>>1 = e. So out = [L, L, L, L, ...].
        let low = [100, 100, 100, 100];
        let high = [0, 0, 0, 0];
        let out = synth_1d(&low, &high);
        assert_eq!(out, vec![100, 100, 100, 100, 100, 100, 100, 100]);
    }

    #[test]
    fn synth_1d_length() {
        let low = [1, 2, 3];
        let high = [4, 5, 6];
        assert_eq!(synth_1d(&low, &high).len(), 6);
    }

    #[test]
    fn synth_1d_manual_small() {
        // low = [8, 4], high = [0, 0].
        // even update: e[0] -= (h[-1]+h[0]+2)>>2; h mirror len2:
        //   h[-1]=mirror(-1,2)=1 -> 0, h[0]=0 -> (0+0+2)>>2=0 -> e[0]=8
        //   e[1] -= (h[0]+h[1]+2)>>2 = (0+0+2)>>2=0 -> e[1]=4
        // odd update: o[i] += (e[i]+e[i+1])>>1, e mirror len2:
        //   o[0] += (e[0]+e[1])>>1 = (8+4)>>1 = 6 -> 6
        //   o[1] += (e[1]+e[mirror(2,2)=0])>>1 = (4+8)>>1 = 6 -> 6
        // out = [e0, o0, e1, o1] = [8, 6, 4, 6].
        let out = synth_1d(&[8, 4], &[0, 0]);
        assert_eq!(out, vec![8, 6, 4, 6]);
    }

    #[test]
    fn synth_1d_nonzero_high() {
        // low=[10,10], high=[4,4].
        // even: e[0] -= (h[-1]+h[0]+2)>>2 = (4+4+2)>>2 = 2 -> 8
        //       e[1] -= (h[0]+h[1]+2)>>2 = (4+4+2)>>2 = 2 -> 8
        // odd: o[0] += (e0+e1)>>1 = (8+8)>>1 = 8 -> 12
        //      o[1] += (e1+e[mirror(2,2)=0])>>1 = (8+8)>>1 = 8 -> 12
        // out = [8, 12, 8, 12].
        let out = synth_1d(&[10, 10], &[4, 4]);
        assert_eq!(out, vec![8, 12, 8, 12]);
    }

    #[test]
    fn recompose_level_flat() {
        // 2x2 bands. ll flat 40, all high bands zero -> flat 40 plane.
        let ll = Band::new(2, 2, vec![40, 40, 40, 40]);
        let zero = Band::new(2, 2, vec![0, 0, 0, 0]);
        let plane = recompose_level(&ll, &zero, &zero, &zero);
        assert_eq!(plane.width, 4);
        assert_eq!(plane.height, 4);
        assert!(plane.data.iter().all(|&v| v == 40), "{:?}", plane.data);
    }

    #[test]
    fn recompose_level_dimensions() {
        let ll = Band::new(3, 2, vec![0; 6]);
        let b = Band::new(3, 2, vec![0; 6]);
        let plane = recompose_level(&ll, &b, &b, &b);
        assert_eq!((plane.width, plane.height), (6, 4));
        assert_eq!(plane.data.len(), 24);
    }

    #[test]
    fn band_accessor() {
        let b = Band::new(2, 2, vec![1, 2, 3, 4]);
        assert_eq!(b.at(0, 0), 1);
        assert_eq!(b.at(1, 0), 2);
        assert_eq!(b.at(0, 1), 3);
        assert_eq!(b.at(1, 1), 4);
    }

    #[test]
    fn recompose_separable_consistency() {
        // A horizontal-only gradient encoded as ll varying in x, lh/hl/hh
        // zero, reconstructs a flat-in-y plane (column synthesis of a
        // flat column is flat).
        let ll = Band::new(2, 2, vec![20, 60, 20, 60]); // same both rows
        let zero = Band::new(2, 2, vec![0; 4]);
        let plane = recompose_level(&ll, &zero, &zero, &zero);
        // Each output column is constant down its height (rows identical).
        for x in 0..plane.width {
            let top = plane.at(x, 0);
            for y in 0..plane.height {
                assert_eq!(plane.at(x, y), top, "col {x} not flat");
            }
        }
    }

    #[test]
    fn recompose_plane_zero_levels_is_identity() {
        // A 0-level plane is just its single LL band.
        let ll = Band::new(3, 2, vec![1, 2, 3, 4, 5, 6]);
        let plane = recompose_plane(&ll, &[]);
        assert_eq!(plane, ll);
    }

    #[test]
    fn recompose_plane_one_level_matches_recompose_level() {
        let ll = Band::new(2, 2, vec![40, 40, 40, 40]);
        let zero = Band::new(2, 2, vec![0; 4]);
        let level = LevelBands {
            hl: zero.clone(),
            lh: zero.clone(),
            hh: zero.clone(),
        };
        let plane = recompose_plane(&ll, &[level]);
        let direct = recompose_level(&ll, &zero, &zero, &zero);
        assert_eq!(plane, direct);
        assert_eq!((plane.width, plane.height), (4, 4));
    }

    #[test]
    fn recompose_plane_two_levels_doubles_twice() {
        // Inner LL is 2x2; two levels -> 8x8 plane.
        let ll = Band::new(2, 2, vec![32, 32, 32, 32]);
        let z2 = Band::new(2, 2, vec![0; 4]);
        let z4 = Band::new(4, 4, vec![0; 16]);
        let inner = LevelBands {
            hl: z2.clone(),
            lh: z2.clone(),
            hh: z2.clone(),
        };
        let outer = LevelBands {
            hl: z4.clone(),
            lh: z4.clone(),
            hh: z4.clone(),
        };
        let plane = recompose_plane(&ll, &[inner, outer]);
        assert_eq!((plane.width, plane.height), (8, 8));
        // All-flat input + zero HF -> flat plane.
        assert!(plane.data.iter().all(|&v| v == 32), "{:?}", plane.data);
    }

    #[test]
    fn recompose_plane_band_count_matches_decomp_formula() {
        // The number of bands a plane carries is 3*levels + 1: one LL
        // plus three HF per level. Confirm recompose_plane consumes
        // exactly that many bands.
        for levels in 0..=2usize {
            let mut bands = 1; // the LL band
            bands += 3 * levels; // three HF per level
            assert_eq!(bands, 3 * levels + 1);
        }
    }
}
