# Ordinary Pause rendering

The retained native pause routine (`0x43b8d0`) composes its pause artwork once,
then services input, audio and events. The port likewise leaves its last
presented frame onscreen during ordinary Pause: it issues no further draw or
swap calls until a redraw is needed. No screenshot, texture cache or readback
is added to the pause path.

Window exposure, diagnostic toggles and World presentation revisions invalidate
the paused frame. Resize/restore permits two draws because double-buffer storage
may adopt its new dimensions on the first swap. Resuming gameplay, opening a
menu/inventory or entering a modal presentation restores normal rendering.
Active saves and pending GLES previews also retain their normal render path.
Explicit screenshot requests force a render.

The paused loop uses a 16 ms event wait even with an uncapped frame setting.
Waiting does not remove or reorder events; existing input/audio/save services
continue. Simulation cadence, movement, animation clocks and save formats are
unchanged. Profiling reports actual presentations and reused paused iterations
separately; paused loop throughput is not rendered FPS.
