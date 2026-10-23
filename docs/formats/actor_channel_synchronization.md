# Actor/channel synchronization reuse

Gameplay synchronizes actor presentation before advancing animation. Drawing can
reuse that synchronization when World has not changed the inputs since then.
This reduces repeated map-wide channel scans without changing clock admission,
animation advancement, native enemy phase selectors, audio, or effect lifetimes.

World exposes a derived, nonserialized render revision. Tick entry, valid spatial
mutation notifications, publication reset, restore, rebind and player-direction
changes invalidate it. Dirty notifications invalidate even if an actor is already
queued. Renderer caches vector identity, size and revision; clear, restore and
channel-scan rebuild invalidate the binding. Post-advance expiry and births cause
a fresh synchronization before draw. Callers without an explicit revision retain
full synchronization. Reuse is restricted to native enemy phases: legacy adapters
can reset stateful Spider and generator channels on each synchronization.

The revision is not a save field. This change does not migrate native visibility
or Graphic-update-needed rules and does not freeze offscreen effects. General
animation advancement still visits the existing ordered channel list.

## Validation

The opt-in original-resource audit compares reuse disabled/enabled in two live
applications across four maps. It checks World and renderer serialized state,
pixels and audio after movement, firing, camera changes, Darkness, Time Warp,
no-tick redraws, file save/reload, post-advance births, direction mutations and
restored held-gate cursors. Output must use an isolated directory.

```sh
cmake -S . -B build-sync-audit -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DVURUS_ACTOR_SYNC_PROFILE=ON
cmake --build build-sync-audit --target actor_channel_sync_audit -j4
SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy ./build-sync-audit/actor_channel_sync_audit /path/to/original/data /tmp/vurus-sync-audit 360
```

Counters and thread CPU timing compile out of normal builds. The audit requires
an OpenGL-capable offscreen SDL driver and original game resources supplied by
the tester. No proprietary fixtures are distributed.

The validated private trial passed 187 tests and 1,440 paired frames, plus an
independent 1,440-frame committed-control trace comparison. Two H700 diagnostic
pairs showed crossfire main-thread CPU means about 1.9–2.9% lower; the final pair
reduced synchronization CPU from 2.0733 to 1.6376 ms. Seven-scene combat counters
matched. Six final scene means improved; one increased 0.9%. These instrumented
CPU windows are not an official FPS gain or full-game parity claim.
