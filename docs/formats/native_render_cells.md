# Camera-cell draw admission

The original `433b30` draw caller starts with the camera's padded cell window,
then traverses the retained cell lists. It does not prepare every map body before
screen rejection. This boundary is shared by desktop OpenGL and handheld GLES.

## Window and order

Original `433b4f..433bcd` reads integer surface translations at world `+100c/+1010`,
viewport dimensions at `+f84/+f88` and map dimensions at `+f74/+f78`.
The X/Y cell sizes are 64/32 surface pixels. For nonpositive translation, negate
as an unsigned 32-bit word, shift by 6/5, subtract the left/top margin 1/2 and
clamp to zero. Positive translation starts at zero. Exclusive right/bottom bounds
are start plus `(viewport >> shift) + 3/5`, clamped to map dimensions.

`433c02..433c70` traverses Y first, then X, then each cell's head/next chain.
In-ground admission precedes ordinary body/scenery admission. The port reads the
existing World actor cells and uses derived object-to-channel indices; it does
not reorder actors, alter birth identities or create a separate spatial grid.
The compositor and sprite assets remain unchanged. Equal-depth overlap pixels
can differ from the old port's birth/channel-index preparation order.

## State and phase boundary

`advanceEnemyPhase(Draw)` supplies the current cell view. Clear/restore discards
that view and rebuilds channel indices. Isolated art fixtures without a cell view
retain their existing unbounded adapter. `setNativeDrawCells(false)` selects that
adapter for paired tests; it is not a player-facing setting.

World-owned explosion cursors remain authoritative. The former global draw walk
also refreshed their renderer mirrors on a birth/rebind frame, including offscreen
effects. A deduplicated pending-binding list preserves that bookkeeping at draw
time, respecting detail/visibility/admission guards. It runs after the caller has
queried finished effects, so it does not retire newborn effects early. It copies
the authoritative cursor without advancing World time, damage, lifetime or RNG.

These indices and binding flags are derived, not serialized. No World, actor,
projectile or save-schema layout changes are required. Animation update policies,
audio phases and physics remain separate from draw admission. This boundary does
not establish complete original graphics-clock or FX-lifetime parity.

## Checks

`native_render_cells_test` covers padding, origin/map edges, wider viewports,
Y/X/head order, same-cell stability and re-entry. The opt-in resource audit pans
across cell boundaries with fractional coordinates, toggles ground details and
performs file save/reload, checking World, renderer state and audio cues together:

```sh
cmake --build build --target native_cell_camera_audit -j4
SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy \
  ./build/native_cell_camera_audit ./work/iso/Vyruz \
  ./work/quirk-audit/camera-cell-check 240
```

Use an isolated output directory. These resource checks and synthetic window
controls are not an original Windows playthrough or full-game visual acceptance.
