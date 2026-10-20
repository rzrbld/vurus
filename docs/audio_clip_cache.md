# Weapon audio clip bindings

`GameAudio::load` resolves each weapon class's start, fire, end and empty-ammo
sounds into shared `AudioClipPtr` bindings. It uses the same normalized resource
paths, preload order and unavailable-resource diagnostics as ordinary clip loads.
A missing/empty sound is a null binding. Clips retain shared ownership of the
existing decoded PCM; the cache creates no PCM copies.

Weapon visits still select the current weapon by name and follow native awake
ordering and the native audio stage machine. Availability checks read the binding;
playback passes it directly to `AudioDevice`, with the same mask, priority, gain,
pan, loop flag, world-pause behavior and effects bus as the path-based wrapper.
The accepted start-only event handling uses the same resolved availability.

`reset` stops effects and clears per-actor stages/owners without discarding the
loaded bindings. `load` resets playback and clears old weapon bindings before
resolving the next scene/language, then prunes unneeded clip resources. No binding
points into a mutable placement/actor vector. Bindings are presentation state and
are never serialized. Speech, embedded samples, engine sounds and music retain
their existing lookup paths and lifetimes.

`AudioDevice::nativeChannelPlaying` rejects channel 0 and channels above 16 before
acquiring SDL's device mutex. `AudioMixer` already defines these values as always
false; they address no mixer state. Valid channels 1–16 still acquire the mutex,
including channel 1's music behavior. Other callback synchronization is unchanged.

`game_audio_test` checks all four weapon sound stages with present/missing clips,
reset/load/language cycles and invalid channels before open/after close. It defaults
to SDL's dummy backend and honors an explicit `SDL_AUDIODRIVER` for device checks.
Its playlist fixtures last 100 ms so asynchronous prefetch can arrive before the
first track ends; the four-transition/one-second assertion remains intact.
