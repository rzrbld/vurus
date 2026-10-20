# Derived world-record indices and hot property caches

Actor rows keep stable IDs for gameplay, references and save continuation. The
collector may reuse eligible released transient slots without erasing/renumbering
rows. These optimizations change derived bookkeeping, not persistence or manager
ordering.

## Reference-owner candidates

`GameWorld::referenceOwnerCandidates_` is an ascending, conservative list of slots
that may own references. Collection reads current state from those slots using
the existing alive/reactivating/held admission and owner, generator, explosion
and AI-route fields. Queued audio, damage, contact, projectile, inventory and
other world-level references retain their separate pin passes.

Construction and accepted restore rebuild membership from current reference
fields and controller/transient roles. Every generic factory birth enters before
its caller can attach delayed ownership, FX or AI state. Held materialization and
code-return births use the same registration boundary; assigning a native creator
also registers its owning slot. Any future producer that attaches references to
an otherwise inert authored actor must register that actor before collection.

Death does not remove membership. Reactivation and delayed assignment therefore
cannot invalidate a cached free/pinned decision: no such decision is cached.
This is a candidate-role index, not an exact list of live actors. Its traversal
cost scales with admitted candidate slots and their routes, not solely live count.

The index belongs to World and contains IDs, not actor addresses. Default World
moves/copies retain it; ordinary scene rebinding does not require address-based
repair. Successful replacement rebuilds it; rejected restore validation leaves
the current world/index intact. Neither the index nor its history is serialized.

Pin storage initialization and the class-specific reusable-slot rebuild remain
unchanged. Recycling still checks all identity, authored-placement, held,
reactivation, reference, native-pool and spatial-cell guards, and preserves the
existing lowest-slot same-class reuse order.

## Resolved hot properties

`nativeSubtype` is already derived at construction/restore. Hot Spider/Walker/
Tank decisions use `nativeDamageSubtype`, retaining its construction-time resolver
fallback instead of repeatedly constructing normalized subtype strings.

`visibleWhenFiring` caches the resolved placement's `Visible-when-firing` flag.
Construction, restore and held materialization derive it again; generic, ordinary
and Spider AI use the cached value. It is not saved. Runtime code that changes
resolved placement properties must refresh their dependent derived caches.

The flag fits existing ActorState padding on the validated desktop/GLES ARM64
ABIs; large optional AI states remain indirect. No broad actor-layout rewrite or
permanent actor-row erasure is introduced.

## Validation

The transient-reuse regression covers referenced-slot protection after restore
and World move/rebind, delayed factory roles, debris reuse and deterministic disk
continuation. Cached-flag checks cover stale in-memory values and unchanged save
serialization. Full-scan oracle variants and real-resource image/save/performance
comparisons are private audit tools, not production switches. Hardware timing is
separate from portable unit-test correctness.
