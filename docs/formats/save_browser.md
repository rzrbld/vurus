# Saved sessions browser

The native executable's42ad70 terminal-save path creates numbered sessions per
player instead of replacing one file. Empty history begins at s000. Each new
save normally leaves seven numbered sessions. The separate default/retry
session is not one of these paid saves.

427c30 builds “Saved sessions” at(265,70),35px row spacing, native fonts3/4.
The title uses font1 at(10,120). Up to seven rows show the original level
name and `%s   [%d h %d mn %d sec] `. Selected help shows energy, shield and
difficulty in the standard lower-right rectangle. Back is initially selected;
a blank spacer precedes it when fewer than seven records exist. The native
comparator sorts session paths in descending lexicographic order, not by date.
A selected save's224×168 TGA preview appears at(8,150).

## Port integration

Normal paid saves now go beside the executable under
`saves/p-<hex-encoded player name>/s000.vzs`,s001.vzs,etc. Seven numbered
checkpoints are retained per player. Encoding the options name keeps each
profile inside one directory, including names containing slashes or Unicode.
Changing the options player name selects that player's history.

The existing `slot1.vzs` remains untouched and appears as an additional legacy
entry. Its content is not copied into the rolling history or counted against
its seven saves. Previously overwritten checkpoints cannot be recovered by
this change. Explicit `--save-file`, `--load-save` and `--write-save` paths keep
their fixed-file behavior; normal interactive saving uses history.

Load Game opens the browser before changing the current session. Keyboard,
mouse and controller selection use the same rows. Choosing a save from an
active game asks for replacement confirmation, with Cancel selected. Failure
or cancellation preserves the current world. Missing or unreadable saved
files report an error; bad entries remain visible rather than silently loading
a different checkpoint. A legacy entry can make eight selectable saves; this
is an intentional compatibility addition to the original seven-row list.

Catalog refresh decodes and validates saved metadata, reads level names from
original3DO headers, and formats labels on one background worker. Save completion
starts a refresh; opening Load Game requests a current catalog without waiting.
Escape can draw its menu while the worker runs. The browser shows a loading row
with Back selected and usable until the new list is ready. This changes the port's
I/O scheduling; it does not change the save format or supported readers.

The main thread polls only ready results. Each carries its save base, profile and
invalidation generation, so a late result cannot replace a newer catalog. Row
invalidation also clears queued save selection and its thumbnail. Results are not
published during a pending save transaction. Catalog reads and paid-save history
publication/pruning share a mutex acquired on workers, so a refresh cannot hold
open a file while history pruning removes it. The interactive thread never waits
on this mutex. The writer releases it before waiting for optional GLES preview
pixels; worker/future declaration order preserves storage and VFS lifetimes on
shutdown. Large decoded saves and temporary parsing state are destroyed on the
worker; only catalog rows and strings cross back to the menu.

New writes publish a complete file using no-replace rename before pruning owned
numbered history; a fallback for filesystems without no-replace rename uses exclusive creation and cleanup
on write failure. The fallback path can be visible while writing. It never
replaces an existing file. Legacy/manual files and symlinks are not pruned.

Preview TGA files are optional and loaded only for the selected catalog entry,
with that selection cached; refresh never bulk-loads thumbnails. Desktop history
saves read the last presented gameplay frame. GLES requests a thumbnail only when saving, captures that frame
before swap and hands the pixels to the background save worker. Snapshot timing
is unchanged; cancellation or readback failure may omit the thumbnail without
failing a committed checkpoint. Synchronous GLES tooling saves capture on their
next render. Both paths resize the top-left640×480 logical area to224×168 with nearest
sampling, and draw it at the native location. Exact native inclusive-endpoint
resampling and physical-versus-logical resolution mapping remain presentation
adapters. Missing previews do not prevent loading; older legacy files need no
preview. Preview failure does not turn an already committed paid save into a
failed transaction.

Evidence: `work/quirk-audit/save-browser/native.md` and retained focused PE
exports. Unit and App integration tests cover history retention, legacy
preservation, profile isolation, safe publication/failure, exact older-session
selection, keyboard/hover parity, cancellation, missing/corrupt saves and
preview images at640×480 and1280×720. App controls also hold catalog/writer
workers at deterministic gates, exercise Back in both in-game and main-menu
browsers, reject late profile/generation results and queued stale selections,
and check fee/byte-exact save continuation. These are static native findings and
port checks, not a native Windows gameplay recording.
