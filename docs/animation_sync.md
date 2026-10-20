# Animation channel synchronization

SceneRenderer retains channel identities for save/restore and advances the
working channel list in stable actor birth order. Synchronization uses compact
ChannelScan metadata; expired channels do not require touching full Channel state
when their visual revision is unchanged.

For an unchanged passive channel without runtime gate/lift state,
syncActorChannel has no work. Keep alive channels in the working list so cursor,
audio, Time Warp and newborn admission still run. Dead channels retain the
clear-once inactive path. Passive excludes native enemies, walkers, spiders,
generators, invisible channels, helicopters, lifts and firing channels.

Refresh scan.visualRevision from the actor before early returns for firing and
native channels. Stable-sort keys use its upper 32 bits: nonzero birth minus one,
otherwise the object index, matching actorRecordOrder. Keep stable sorting and
tie order; a Channel revision may deliberately lag actor state until its phase.
Normal channel synchronization still copies its resulting revision to the scan.
No persistent state, layout or animation cadence changes.

The opt-in enemy_animation_phase_audit includes a synthetic sound-bank fixture
for unchanged cursor/audio continuation, zero-birth fallback, changed birth
ordering/ties, death/recycled-newborn admission, runtime gate state and restore.
Its original-resource controls cover native phases, Time Warp, fades and mixed
newborn/restore behavior. Build the target explicitly and run with DATA OUTPUT.

Actual H700 comparison: four 1,200-frame ABBA runs after 120 warmup frames, crowded CAVE,
640x480 uncapped/VSyncOff, normal Ports/ALSA/music. Animation median 2.729→1.966 ms,
CPU update 9.748→9.041 ms. World/render roughly unchanged. Different live windows
and populations; worst World/update spikes were not improved. This is not a
full campaign parity or universal frame-time guarantee.
