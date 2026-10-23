# Animation sample-bank selection

The original selector at0x45ea70 requires command mask0x1 and a
nonzero one-based sample index. It uses the shared bank at process52766c only
when that pointer is nonnull and command mask0x10000 is set; otherwise it uses the
animation's local bank (ANM+0x8). The selected bank's count at+0x34 bounds the index;
its sample table at+0x3c supplies the sample. An invalid shared-bank index does
not cause a second lookup in the local bank.

GAF28-byte subframes store command/sample at decimal offsets16/20
(hex0x10/0x14), the same offsets used by the runtime selector. These command bits are
separate from subframe drawing flags. The retained original executable SHA256
is415f40822f0475808caa019122c1d3dd15da6c5e94ebceeffddc4b3a6134bc46.
This contract comes from static original instructions, not Windows listening.

Actor and terrain renderer producers retain the local bank path, sample index,
ANM identity, spatial source and the shared-bank request in SceneAudioCue.
GameAudio resolves the bank before validating the index and uses the existing
native channel mask/priority, distance, pan and ownership rules. Held/Forget
commands continue to address the playing handle, independently of bank choice.

GameAudio::load accepts an optional explicit shared-sample-bank resource path
following the terrain argument. Empty/default means no shared bank; flagged
commands then use the local bank. No resource filename or authored attribute
is inferred. A successfully decoded bank with zero samples is present and
rejects all nonzero indices; missing/malformed banks cannot be registered and
retain the unbound fallback. The loader records decode success separately from
cached failures to preserve that distinction.

All samples are cached during load; playback does not read resources. Transient
reset retains loaded bindings; the next load explicitly replaces/unbinds its
shared-bank selection and prunes unused cached banks. These are host audio
bindings, not persisted World state, so no save schema changes.

The verified shipped vz-data.rsc contains179GAF entries,177populated banks,
11172subframes and56sample commands; none request0x10000. The other two GAFs are
empty roots, and Vz-slide.rsc has no GAF entries. The normal game therefore
registers no invented shared bank. This repair restores the generic selection
contract and its local fallback; it does not claim a previously audible shipped
failure or an FPS benefit.

The shared_sample_audio test uses independent synthetic GAF/PCM resources to
exercise actual actor/terrain renderer producers and the SDL dummy mixer:
absent shared bank, flagged/unflagged selection, invalid/zero indices, valid
empty versus failed banks, reset/cache retention and no playback-time reads.
Existing shadow admission and the separate256-start producer guard are retained.
