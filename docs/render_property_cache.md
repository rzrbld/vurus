# Authored sprite property cache

`SceneRenderer` owns a lazy cache indexed by placement ID. `SpritePropertyCache`
resolves authored `Altitude`, `Use-delta-packing` and `Thrust/Max`; live actor
projectile altitude still overrides authored height. The same renderer code is
used by desktop OpenGL and handheld GLES. Texture formats, shaders, ordering,
animation cursors and gameplay scheduling are independent of this cache.

The cache retains the placement's `SharedProperties` snapshot. Repeated access
compares vector identity in constant time, without hashing or scanning nodes.
Keeping a strong snapshot reference forces subsequent normal property writes to
detach, even when the placement previously had unique ownership. It also prevents
an old vector address from being freed/reused while the cache still recognizes
it. Replaced/recycled placements resolve again if their properties differ;
reuse of an identical class snapshot needs no work. As with all SharedProperties
users, mutations must go through its copy-on-write API rather than a mutable
reference retained across readers.

Cached values and snapshot identity are presentation state and are not saved.
Renderer clear releases them; fresh renderers after level load resolve from the
loaded placements. Restoring animation state cannot restore stale property values.
No references into the growable placement or cache vectors survive a draw
submission or a cache growth operation.

Drawing historically parses altitude directly with `strtof`, accepting prefixes
and nonfinite text. Orbit attachment uses the existing finite-value
`propertyNumber` parser. Both derived values are retained separately to preserve
that distinction. Delta packing and maximum thrust use the original helpers.
Dynamic Only-base behavior remains in ActorState; its property write also
invalidates the snapshot normally.

Tests cover parsing, empty/missing properties, mutation with and without other
snapshot owners, replacement, Only-base changes and cache moves. Device image,
save and timing evidence is retained locally under
`work/quirk-audit/progressive-slowdown/render-property-cache/`.
