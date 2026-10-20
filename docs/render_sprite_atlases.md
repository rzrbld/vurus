# GLES sprite and font atlases

`Gfx::uploadSpriteTexture` packs compact GAF animation frames into shared texture
pages on GLES. The scene renderer uses it for enemies, vehicles, projectiles and
FX; the HUD's font-layer path uses it for glyphs, including menu and inventory
fonts. Terrain retains its separate existing atlas. Desktop OpenGL uses the
existing standalone texture upload and draw path.

Returned frame handles are logical texture identities. Draw them through
`Gfx::beginQuads`/`quad` and release them with `Gfx::deleteTexture`; do not bind
these handles through raw GL. Their stable slots continue to work with the scene
texture residency cache. Only backing pages own atlas pixel storage. The GLES
batcher sees the page texture, allowing adjacent compatible sprites/glyphs to
merge without sorting or changing painter order, tint, alpha tests or blending.

Each region has a replicated one-texel border. `beginQuads` maps normalized
frame coordinates into its interior; flipped and clipped UVs are preserved.
Nearest world sprites, bilinear world sprites and nearest font glyphs use
separate pages. Bilinear copies use the existing transparent-RGB dilation.
Trilinear sprites retain the existing independent weighted-alpha mip chains:
whole-page mip generation would mix neighboring frames. Their unfiltered draw
operations and font glyphs can still use atlases. Filtered atlas draws require
the matching compact source bitmap, borrowed only for that call.

World pages normally measure512×512; font pages256×256. Larger world frames
can use1024×1024, clamped to
the queried hardware limit. Oversized frames or allocation overflow use standalone
textures. World pages are capped at32MiB; font pages at2MiB. Regions are allocated
lazily, holes are reused/coalesced after eviction, and empty pages are deleted.
No full animation-bank preload or retained RGBA duplicate is introduced.

World page slack counts toward the existing32MiB residency budget in addition
to frame payload and retained filter copies. Allocation/occupancy totals are
maintained incrementally. Trimming re-evaluates slack after each eviction because
freeing a region can increase page slack or release a page. As before, a visible
working set may exceed the residency budget to avoid evicting current draws.
The separate1024-frame limit remains. Font overflow preserves the prior lazy
standalone behavior rather than discarding glyphs.

Visible scene frames/filter copies are prepared before sprite drawing. Each
font string uses the same native layout to prepare its glyphs before drawing,
without preloading the whole font bank. Grouping uploads and using smaller font
pages reduces writes to pages already sampled by queued geometry.

Uploads, region release/reuse and page deletion complete earlier queued draws
before changing their storage. Ordinary non-atlas draws reset the UV mapping.
This work changes presentation/storage only, not simulation or save formats.

`render_backend_test` compares atlas and standalone pixels across filter changes,
odd dimensions, flips, clipping, magnification/minification, overlap order,
region reuse and allocation overflow. Run it in desktop GL and GLES builds.
Host software rendering establishes correctness and draw-count changes only.
H700/Mali image/save controls and repeated1200-frame comparisons also pass:
live reciprocal median18.9->20.2FPS, paused54.0->58.8FPS, about3.2MiB extra
steady RSS in the generated crowded CAVE/T1C2 fixture. This is a scoped device
result, not a full campaign or other-device performance claim.
