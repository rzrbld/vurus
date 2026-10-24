# User-owned disc import

The supported startup contract is a game-data directory, an ISO9660 ISO,
a raw MODE1/MODE2 2352-byte-sector BIN, or a single-BIN CUE. `launch.sh`
passes its own directory, independent of the working directory. Bare `vurus`
also searches beside its executable by default. `VYRUZ_DATA_DIR` or `--data`
overrides the selection; an explicit command-line path wins.

Normal launch opens an asset-independent SDL setup screen before preparation.
Resource archives/movie extraction and CD-track ripping report actual bytes for
the current file (CD labels also show track count). Checking/final synchronization
use an indeterminate current-task bar; Overall retains known aggregate progress. I/O runs on a worker while the main thread draws and
handles resize, keyboard, mouse and mapped controllers. Cancel is Esc/controller B
or the on-screen button; errors offer Enter/controller A to retry, or Esc/B to
exit. Hot-plug/fallback and focus/reconnect neutral guards use the gameplay
controller adapter. `--controller-map` is admitted before setup input too.
Successful preparation proceeds automatically into the existing game UI; cached
launches only perform their quick checks. Original game assets are not required
for this independently authored bootstrap font/screen. `--check-data` retains
its windowless path.

On a folder launch, existing loose/archive resources take precedence. Otherwise
an existing successful import receipt is reused, then a single disc image is
auto-selected. A CUE and its referenced BIN count as one image; multiple images
require explicit `--data`. CUE filename matching is case-insensitive. Explicit
BIN selection uses a same-stem adjacent CUE if present, to preserve CD music.

The importer reads the disc without modifying it. ISO paths are used to locate
`VYRUZ/VZ-DATA.RSC`, `VYRUZ/VZ-SLIDE.RSC` and optional `VYRUZ/Intro.avi`
(or those files at disc root).
It does not run Windows installers or extract arbitrary paths from the image.
CUE `INDEX 01` offsets define the music tracks. A BIN without a CUE or a normal
ISO has no known CD track table, so the game uses its existing WAV music fallback.
The optional movie is preserved byte-for-byte alongside the archives. Its absence
does not prevent importing or playing the game; decoding is handled by playback.

Imported data is staged in `data/` beside the resolved executable, even when
`--data` selects a disc elsewhere. The game folder must be writable; there is
no silent home-directory fallback. Both archives are structurally checked;
the optional movie, when present, and all selected music tracks must finish writing. Files and directories are
synchronized before atomic publication. Failed imports discard their own staging
directory, leave sources untouched and do not publish a location receipt.
A completed import publishes `data/current.txt` containing a relative import key;
external source folders also retain their own selection receipts. Normal launches
reuse the selected import before scanning local images, including when the image
has been deleted or only its CUE remains. Moving the whole game folder together
with `data/` preserves discovery. Explicit disc/resource paths retain precedence.
A damaged explicitly selected image fails instead of falling back to old data.
There is no import-complete dialog or deletion reminder in the game. The packaged
README explains that after successful unpacking/startup the original image may
be deleted manually. The game never deletes source media.

Older builds' home-directory imports are not automatically moved or deleted.
Copy the contents of the desired old import (archives, optional movie and music/)
into `data/` beside the new binary, or supply the source disc again. Valid archives
directly in `data/` are recognized when there is no selected indexed import.
Existing external resource directories still work with `--data`. Save files and
preferences retain their existing paths; this change concerns imported assets.

Old imports missing `Intro.avi` are upgraded on launch when their original image
is still present and its path, size, modification time and CUE contents match the
receipt's import key. Recovery stages and synchronizes the movie before renaming
it into the existing import. Missing, damaged or unrelated sources never prevent
using a valid old import. A movie that was never imported cannot be recovered
after source deletion: restore the owned image and select it with `--data` to
import it again (a changed path/timestamp creates a new complete import).

A successful receipt takes precedence over new images dropped into the same
folder. To import a different image deliberately, use `--data /path/to/new.cue`.
Changing a selected image's path/size/modification time or CUE contents creates
a new cache key. Keys are identifiers, not cryptographic integrity proofs.
Move the entire `data/` folder along with the binary and launcher. Imports are reusable data, not disposable caches; deleting them after
deleting the image requires restoring the original image.

Use `--check-data` for windowless startup validation and actionable errors. The
RSC reader checks header/table/name/payload bounds, rejects short reads and
clears previous archive state after failure. Loose resource lookup is indexed
case-insensitively, detects ambiguous casing and rejects traversal. A single
legacy leading `../` in authored resource names remains supported.

Current boundaries: ISO9660 primary volume only, 2048-byte logical blocks,
single-file CUE with MODE1/2352 or MODE2/2352 data followed by audio; no CHD,
MDF, compressed images, multi-file CUE, UDF-only, multi-extent or extended-
attribute ISO files. Unsupported or damaged images fail with an error. No
media hash/edition whitelist is imposed. A modified-but-structurally-valid
archive still needs its normal resource decoders and gameplay checks.

Portable-import verification (2026-10-13):32 suites pass, including source deletion,
folder relocation, external explicit sources, legacy direct data layout, invalid
explicit images and an unusable destination. Original BIN/CUE import retained both
archives, Intro.avi and7 CD tracks under the executable's data/ folder. After
renaming that game folder, a bare binary launch from /tmp with --check-data reused
the local selection successfully. Logs are in work/portable-import-audit/.
That checkpoint is historical; progress/cancellation and the reliability checks
below are now implemented.


## Import reliability — 2026-10-17

The Linux importer locks the data directory while preparing/selecting imports.
A second launcher gets a Retry error instead of modifying the first launcher's
staging. This does not affect in-game simulation, movement, rendering or saves.

A new staging directory has a durable `.vurus-staging` ownership marker. On retry,
owned interrupted stages are discarded and extraction restarts from the unchanged
image. Empty pre-marker stages and reserved receipt/movie temporary files are
also retired. Unmarked nonempty directories and symlinks are preserved. Existing
older unmarked interrupted stages are deliberately not recursively deleted.
A complete directory published before a crash can be reused on the next explicit
source retry; completed installations are never deleted by recovery. If a crash
occurs before its receipt is selected, keep the source image until retry finishes.
This is process-interruption recovery, not a physical power-loss certification.

Before new extraction, available space is checked against both archive extents,
optional movie, all CUE audio extents/WAV headers and a 1 MiB metadata allowance.
Space can change afterward and quotas may differ; real write/close/fsync errors
still fail with a readable message. Cancellation and failures remove only the
current transaction's staging and receipt temporary files. Pre-publication errors
preserve the prior selection. A failure after an atomic directory/receipt rename
may leave a complete new import/selection for retry, never a partially written
selected archive. Existing imports and source media are not deleted or repaired
in place. A damaged completed installation requires restoring from owned media;
it is not silently overwritten.

New imports contain `.vurus-files-v1`: relative output names, byte sizes and
64-bit FNV-1a checksums of both archives, optional movie and ripped WAVs. This is
accidental-corruption detection, not cryptographic authentication or an edition
whitelist. Ordinary cached launches check archive structure and recorded sizes,
without rereading every music file. `--check-data` and `--check-data-all` explicitly
verify full recorded checksums. Legacy archives/loose trees remain supported
without a manufactured checksum history; their decoders still validate resources.
The RSC parser rejects overlapping header/table/payload ranges and empty archives;
the duplicate name actually present in the supplied original archive is preserved.

`--check-data-all` is windowless and validates all 25 shipped map entries through
the existing level, terrain/native-tile, content/World, placed animation and HUD
loaders. It is optional tooling, not a new full-campaign startup delay. It is not
an all-weapon playthrough or proof of every unused codec/resource combination.

Validation and local artifacts are recorded in
`work/quirk-audit/import-reliability/README.md`. No original assets are packaged.


## First-run copy dialog

First-run setup uses a resizable SDL window (initially800×600), with a centered
640×480 logical canvas. The DOS copy modal uses the benchmark/debug dark green
and amber palette, double frame and two60-cell segmented bars. Current task
shows the current file or verification read; Overall counts all copied payload
and subsequent verification reads. Source/destination, task name and elapsed
time remain visible. Unknown totals animate rather than displaying a fabricated
percentage. Error/retry and safe cancellation use the same modal style.

The bootstrap runs before original menu font assets can be loaded, so it keeps
an independently authored small pixel font rather than packaging game artwork.
No new font dependency is required. SDL logical sizing preserves proportions
when resized and maps mouse input back to the same button rectangles. Existing
keyboard/controller retry/cancel behavior, import staging, checksums and atomic
publication are retained.

`ImportProgress` retains its stage/item/completed/total fields and appends
`overallCompleted`/`overallTotal`. The full-import plan counts resource/movie
bytes twice (copy and checksum), CD audio payload twice and each generated WAV
header once (checksum only). Discovery/cached selection/intro-only backfill may
have an unknown aggregate total. No per-chunk additional disk reads are added.
