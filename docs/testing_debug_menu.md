# Debug Commander

Launch `vurus --debug`, then press **F2**, or open the game menu and select
**Debug / testing**. Ordinary launches hide this menu and ignore F2. These are
port testing controls, not an original-game menu.

The screen uses the benchmark results' dark background and gold font, enlarged
by15%, with DOS Commander double frames and two panes. Actions remain on the
left. The right pane shows only the selected item's help or its scrollable
child list. Level and Surprise lists show ten rows and retain their selection
and viewport when switching panes.

- Up/Down or controller D-pad: browse; Enter/A: apply; Esc/B: back.
- Tab: switch panes; Right: enter a child list; Left: return to actions.
- Page Up/Down: scroll ten entries; Home/End: first/last entry.
- Mouse: hover/select either pane, click to apply, wheel to scroll.

Actions:

- **Level select:** start a fresh unsaved session on any of25 shipped map IDs,
  including the demo. Failure preserves the current session. Successful loading
  discards current unsaved progress, without writing disk saves.
- **Enemy AI:** enable/disable enemy AI updates; existing projectiles and player
  controls continue.
- **God mode:** prevent player damage; does not revive a dead player.
- **Fill all ammo:** supply authored equipped weapons/ammunition and fill their
  stock maxima. This is repeatable.
- **Add money:** add exactly1,000,000 unless the complete amount would overflow;
  does not award kills or lifetime income.
- **Surprises:** select an outcome or clear the active effect; normal activation
  delay and effect behavior remain, then gameplay resumes.
- **On-screen info:** hide/show diagnostic FPS/frame timing, memory/object
  statistics and gameplay God/AI labels. F2 and testing controls remain available.
  The ordinary Video FPS counter is independent. Diagnostics never cover the
  Commander screen itself.

The info preference lasts for this application session and survives level
changes; it adds no profile/save fields. AI/God reset on ordinary new-game,
checkpoint load and restart, but carry through debug level changes. Ammo and
money are normal gameplay inventory changes and can be saved intentionally.

FPS/frame averages refresh every half second and include rendering, swap and
pacing. Memory figures refresh once per second: RSS/PSS, anonymous/file/shared
pages and the alternate private/shared split. Do not sum both views. `ROW DATA`
counts inline actor/placement storage, not all live RAM. Unsupported values show
N/A. These diagnostics do not synchronize the GPU or trim memory.

## Local rendering and input audit

With user-owned game resources available:

```sh
cmake --build build --target debug_commander_audit
SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy build/debug_commander_audit DATA_DIRECTORY PRIVATE_OUTPUT_DIRECTORY
```

This opt-in audit checks the actual application at640x480 and1280x720: release
gating, F2, pane focus/preview, continuous scrolling, Page Up, retained selection,
mouse/wheel, info-off/on rendering and unchanged encoded World during the info
toggle. Captures require visual inspection. Original assets are not test fixtures
and must not be redistributed. This does not claim Windows or hardware parity.

## Audio diagnostics

The existing on-screen information overlay also reports active audio voices by
role, rejected/stolen requests, underruns, input/output peaks, output clipping,
minimum limiter gain and callback average/max time. Counters are cumulative for
the current device session; input overloads are not clipped output. See the
[scene audio contract](formats/audio_pipeline.md) for profiles and tuning.
