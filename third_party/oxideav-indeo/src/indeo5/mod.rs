//! Intel Indeo Video Interactive 5 (`IV50`) — clean-room decode.
//!
//! Spec source: `docs/video/indeo/indeo5/spec/` (chapters 00..08).
//!
//! Indeo 5 is a wavelet-subband codec, structurally distinct from the
//! VQ-based Indeo 3 ([`crate::indeo3`]). A coded frame is a
//! bit-packed header stack (`spec/01` picture-start triplet, `spec/02`
//! GOP / frame / band headers) followed by per-band, per-tile
//! coefficient data that an inverse Slant transform and wavelet
//! recomposition turn into pixels (`spec/05`-`spec/08`).
//!
//! This module builds the decode stack from the bottom up. Landed so
//! far:
//!
//! * [`BitReader`] — the LSB-first 32-bit-accumulator bit reader
//!   (`spec/00 §3`, `spec/01 §3.1`).
//! * [`FormatDescriptor`] — the `spec/01 §2` format-descriptor
//!   preamble (magic + dimensions validation).
//! * [`PictureStart`] — the `spec/01 §3` picture-start triplet (PSC +
//!   frame_type + frame_number + the §3.4 NULL soft-correction).
//! * [`pic_size`] — the `spec/02 §1.6` standard picture-size tables.
//!
//! The bitstream payload of a real Indeo 5 frame (GOP / frame / band
//! headers and the per-tile coefficient stream) is parsed by the
//! later modules as they land.

mod assemble;
mod band;
mod bitreader;
mod checksum;
mod chroma;
mod clip_table;
mod codebook;
mod decode;
mod finalise;
mod format;
mod frame;
mod gop;
mod header;
mod level_table;
mod mb;
mod mb_header;
mod mc;
mod mv;
mod output;
mod pack;
// internal — exposed for tests/fuzz; not part of the stable API
#[doc(hidden)]
pub mod pic_size;
mod picture;
mod planes;
mod quant;
mod refbuf;
#[cfg(feature = "framework")]
pub mod registry;
mod rv_table;
mod session;
mod slant;
mod tables;
mod tile;
mod tile_header;
mod transform;
mod verify;
mod wavelet;
mod yuy2;

pub use assemble::AssembleError;
// internal — exposed for tests/fuzz; not part of the stable API
#[doc(hidden)]
pub use assemble::assemble_frame;
pub use band::BandError;
// internal — exposed for tests/fuzz; not part of the stable API
#[doc(hidden)]
pub use band::{BandFlags, BandHeader, DEFAULT_RV_TAB_SEL, MAX_RV_CORR};
pub use bitreader::BitReaderError;
// internal — exposed for tests/fuzz; not part of the stable API
#[doc(hidden)]
pub use bitreader::{BitReader, MAX_READ_BITS};
// internal — exposed for tests/fuzz; not part of the stable API
#[doc(hidden)]
pub use checksum::{
    frame_checksum_present, parse_band_checksum, parse_frame_checksum, ChecksumField,
    FRAME_CHECKSUM_FLAG,
};
// internal — exposed for tests/fuzz; not part of the stable API
#[doc(hidden)]
pub use chroma::{upsample_chroma, ChromaSubsampling};
// internal — exposed for tests/fuzz; not part of the stable API
#[doc(hidden)]
pub use clip_table::{
    build_clip_table, clip_lookup, CLIP_BIAS, CLIP_LOWER, CLIP_PIXEL_CENTRE, CLIP_TABLE_LEN,
    CLIP_UPPER,
};
pub use codebook::CodebookError;
// internal — exposed for tests/fuzz; not part of the stable API
#[doc(hidden)]
pub use codebook::{
    Codebook, Codeword, HuffContext, BLOCK_HUFF_PRESETS, DEFAULT_PRESET_ID, MAX_ROWS,
    MB_HUFF_PRESETS,
};
pub use decode::{
    decode_intra_picture, BandReconstruction, BandTrace, BlockCoding, BlockRecord,
    BlockStreamFault, DecodeError, DecodeFrontier, DecodeStats, DecodedPicture, FrontierReason,
    MAX_PICTURE_PIXELS,
};
// internal — exposed for tests/fuzz; not part of the stable API
#[doc(hidden)]
pub use finalise::{
    frame_produces_output, is_output_written, mark_output_written, output_row_order,
    reference_rotation, DecodeReturn, ReferenceRotation, RowOrder, OUTPUT_WRITTEN_FLAG,
};
pub use format::{ChromaLayout, OutputFormat};
// internal — exposed for tests/fuzz; not part of the stable API
#[doc(hidden)]
pub use format::{
    BI_RGB, FOURCC_I420, FOURCC_IF09, FOURCC_IYUV, FOURCC_YUY2, FOURCC_YV12, FOURCC_YVU9,
};
pub use frame::{FrameError, FrameFlags, FrameHeader, GopTrailer, HuffDesc};
pub use gop::{
    BandInfo, DecompLevels, GopError, GopFlags, GopHeader, Subsampling, TransformId, Transparency,
};
// internal — exposed for tests/fuzz; not part of the stable API
#[doc(hidden)]
pub use gop::{BLK_SIZE_TABLE, MB_SIZE_TABLE};
pub use header::{FormatDescriptor, FrameType, HeaderError, PictureStart};
// internal — exposed for tests/fuzz; not part of the stable API
#[doc(hidden)]
pub use header::{MAGIC_ALTERNATE, MAGIC_CANONICAL, MIN_HEIGHT, MIN_WIDTH, PICTURE_START_CODE};
// internal — exposed for tests/fuzz; not part of the stable API
#[doc(hidden)]
pub use level_table::{build_level_table, level_value, LEVEL_TABLE_LEN};
// internal — exposed for tests/fuzz; not part of the stable API
#[doc(hidden)]
pub use mb::{
    blocks_per_mb, mb_stride, partial_mb_pad_rows, Macroblock, MbBlock, MbGrid, BAND_INDEX_FLAGS,
    BLK_STRIDE_TABLE_A, BLK_STRIDE_TABLE_B, FOUR_BLOCK_X, FOUR_BLOCK_Y, FOUR_BLOCK_Z,
};
pub use mb_header::MbHeaderError;
// internal — exposed for tests/fuzz; not part of the stable API
#[doc(hidden)]
pub use mb_header::{effective_mb_quant, Cbp, MbContext, MbHeader, QdeltaMode, MAX_QUANT};
pub use mc::McError;
// internal — exposed for tests/fuzz; not part of the stable API
#[doc(hidden)]
pub use mc::{mb_uses_mc, mc_add_block, BandView, MC_TRANSFORM_FLAG_MASK};
// internal — exposed for tests/fuzz; not part of the stable API
#[doc(hidden)]
pub use mv::{resolve_mv, McMode, Mv, MvPredictor, MvResolution, ResolvedMv, MV_DELTA_PRESENT};
pub use output::OutputError;
// internal — exposed for tests/fuzz; not part of the stable API
#[doc(hidden)]
pub use output::{
    bias_and_clamp, plane_stride, OutputPlane, ReconstructionPlane, OUTPUT_BIAS, OUTPUT_CLAMP_MAX,
    OUTPUT_CLAMP_MIN, PLANE_STRIDE_ALIGN,
};
pub use pack::{HostBuffer, PlanePlacement};
// internal — exposed for tests/fuzz; not part of the stable API
#[doc(hidden)]
pub use pack::pack_planar;
pub use picture::{PictureError, PictureHeader};
pub use planes::PlaneRole;
pub use yuy2::{pack_yuy2, upsample_chroma_2x};
// internal — exposed for tests/fuzz; not part of the stable API
#[doc(hidden)]
pub use planes::{num_bands, FramePlanes, OUTPUT_ITERATION_ORDER, PLANE_RECORD_ORDER};
pub use quant::{
    dequant_level, quant_group, quant_matrix, recon_value, GROUP_4X4, QUANT_BASE, QUANT_GROUPS,
    QUANT_LEVELS, QUANT_SCALE,
};
#[doc(hidden)]
pub use quant::{BandQuant, OBSERVED_CLASS};
pub use refbuf::RefSlots;
#[cfg(feature = "framework")]
pub use registry::{
    codec_id_for_fourcc, decode_video_frame, make_decoder, probe, register, register_codecs,
    Indeo5RegistryDecoder, CODEC_ID_STR, INDEO5_FOURCCS, PROBE_CONFIDENCE_HEADER_OK,
    PROBE_CONFIDENCE_TAG_ONLY,
};
pub use rv_table::RvTableError;
// internal — exposed for tests/fuzz; not part of the stable API
#[doc(hidden)]
pub use rv_table::{
    escape_lindex, escape_value, run_advance, RvEntry, RvSlotData, RvTable, RV_TABLE_SLOTS,
};
pub use session::{Indeo5Decoder, SessionError, SessionOutput};
// internal — exposed for tests/fuzz; not part of the stable API
#[doc(hidden)]
pub use slant::{
    a0_fragment, b0a_fragment, b1a_fragment, dispatch_use, pair_add, pair_normalise, pair_pack,
    pair_ror1, pair_ror17, pair_ror2, pair_unpack, DispatchUse, HandlerCluster, Page0Handler,
    PairRotate, BLOCK_BYTES, BLOCK_ROW_STRIDE, DEQUANT_FUSED_MASK, PAGE0_ROW_PASS_HANDLERS,
    PAIR_NORM_MASK,
};
// internal — exposed for tests/fuzz; not part of the stable API
#[doc(hidden)]
pub use tables::{
    dequant_scale, DEQUANT_SCALE_BITS, DEQUANT_SCALE_DEFAULT_BITS, DEQUANT_SCALE_LEN, VLC_END,
    WAVELET_SYNTH_CONSTANTS, WAVELET_SYNTH_ROUND_BIAS,
};
pub use transform::{
    inverse_slant_2d_4x4, inverse_slant_2d_8x8, inverse_slant_4, inverse_slant_4_second_pass,
    inverse_slant_8, inverse_slant_8_second_pass, inverse_slant_col_4x4, inverse_slant_col_8x8,
    inverse_slant_row_4x4, inverse_slant_row_8x8, place_scan_4x4, place_scan_8x8, SCAN_COLUMN_8X8,
    SCAN_RASTER_4X4, SCAN_RASTER_8X8, SCAN_ZIGZAG_4X4, SCAN_ZIGZAG_8X8, SECOND_PASS_DC_BIAS,
    STAGE1_ROTATE_BIAS, STAGE2_ROTATE_BIAS,
};
// internal — exposed for tests/fuzz; not part of the stable API
#[doc(hidden)]
pub use tile::{tile_count, Tile, TileGrid};
// internal — exposed for tests/fuzz; not part of the stable API
#[doc(hidden)]
pub use tile_header::{
    explicit_size_matches, tile_predictor_active, TileDataSize, TileHeader, MAX_TILE_DATA_SIZE,
    TILE_SIZE_ESCAPE,
};
pub use verify::ChecksumStatus;
// internal — exposed for tests/fuzz; not part of the stable API
#[doc(hidden)]
pub use verify::{band_checksum, frame_checksum};
// internal — exposed for tests/fuzz; not part of the stable API
#[doc(hidden)]
pub use wavelet::{recompose_level, recompose_plane, synth_1d, Band, LevelBands};
