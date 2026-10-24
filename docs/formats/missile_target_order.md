# Missile target ordering

Guided Missile launch and tracking reacquisition follow each spatial cell’s
maintained actor head/next chain. Actor IDs do not decide which eligible target
wins within a cell. Birth and cell re-entry prepend; movement within the same
cell preserves order. Existing saved arrival ordinals reconstruct these chains.
No save-schema change is required.

Launch retains expanding square-ring, row and column priorities; tracking
reacquisition retains its distinct x-major/y-minor priorities. Eligibility,
range, heading cones, fuse timing and steering are unchanged. The World adapter
finds occupied heads through actor slots and follows their chains without
scanning every map cell or sorting candidates.

The original executable’s `0x41e1e0` reads cell heads at `0x41e2e7–0x41e2f4` and follows
actor+8 links at `0x41e370`; tracking `0x42da80` also walks cell lists. Controlled original
instruction execution with no call hooks confirms that reversing same-cell
chain order reverses launch selection. This is not a Windows playthrough.

The World regression covers birth/re-entry, same-cell movement, reacquisition,
all 24 arrival permutations of four targets after disk restoration, and 60 ticks
of identical full-state save continuation. An isolated original-resource
Missile control also selects the native cell head. These checks establish the
repaired ordering boundary, not complete campaign guidance equivalence.
