# Native navigation query ordering and precision

The shared navigation query follows the original graph's ordered axis lists.
When a source point is enrolled, overlap40f840 and line queries40f9b0/40fce0
start at its adjacent nodes and traverse toward the destination. Starting from
an axis-list end can miss an obstruction:40e630 can leave a moved minimum at
the list head or a moved maximum at the tail temporarily unordered. Preserve
that insertion/update history, eligibility filtering and first-hit ordering.
Unenrolled sources retain the separate coordinate-based fallback.

For X-directed line intersections,40fa8c..40faae computes the slope using x87
extended subtraction and division. It stores a float slope with FST, retaining
the extended quotient to calculate the intercept. The intercept is then stored
as float.40fc21..40fc2d multiplies the stored slope by the edge coordinate and
adds the stored intercept in extended precision before storing the final float
intersection. Do not round the X quotient before calculating the intercept,
or round the multiplication before the final sum. The implementation calculates
the two coefficients once per axis scan.

The Y query differs:40fdcf stores and pops the slope, then40fdd5 reloads that
rounded float before40fdd9/40fddd computes the intercept. Cop subtype16 opts
into this stored-slope intercept through `nativeIntersectionPrecision` on the
graph query. Other callers and the vector overload preserve their accepted
coefficient behavior. The Cop rebase retains the newer shared source-adjacent
traversal rather than restoring the older preview's global scan.

Cop also opts into42d820 corridor comparisons: bounds span plus cached movement
magnitude is rounded once to float; endpoint plus/minus this padding stays
extended until comparison with the actor center. Other families keep their
existing corridor comparisons. Coincident retained route start/target uses
heading0 through42c9d0/4075c0, while physical contact still uses sentinel24.
The accepted Cop action-marker retention remains an intentional deviation.

This rounding affects inclusive obstacle-boundary tests. A false obstruction can
change wave status and branch populations. In particular, native Seeker kind6
abandons a Working(-2) result, whereas a direct Found(-1) result admits a route.
The shared helper therefore affects Jeep and other native navigation users.
This correction leaves corner expansion, route scoring, RNG, movement and
serialization unchanged.

`native_navigation_test` covers source-adjacent X/Y line traversal in both
directions, overlap, temporarily unordered boundaries, filtering and graph copy.
`jeep_navigation_precision_world_test` uses synthetic fractional geometry with
the authored Jeep radius25. It checks graph/vector queries and transposed axes,
the live World search admission, raw direct-wave status/population, two retained
route points and60byte-identical memory/disk continuation ticks. The earlier
float-intermediate implementation fails the clear-line assertion.

Validation also compared retained execution records from original instructions:
460 basic and6,896fractional wave/order/selection records,13,122fixed-layout
queries,65,610head/tail-history queries,4,096corner cases,128scores and4,096range
gates. The corrected desktop x86_64 implementation matches these bounded corpora.
Graph/search probes hook allocation to zeroed blocks, freeing to a no-op and
seen-array creation to preallocated storage; they retain the original navigation
decision instructions. Synthetic flags/static bodies omit allocation reuse,
moving contacts and the Windows host clock. They are original instruction probes,
not an original Windows gameplay session. Natural campaign frequency and complete
moving/contact equivalence remain unproven. Tests require no proprietary assets.

`cop_navigation_world_test` adds query/corridor/heading groups, including a
retained original Y-query counterexample at (1152,221) to (1280,236), controls
for accepted X arithmetic and other families, and540exact memory/disk
continuation ticks. Each group rejects the parent World caller. The rebased
Cop query matches316,872retained original-instruction outcomes across four
overlapping synthetic corpora. These checks establish bounded arithmetic and
live integration, not campaign-wide or Windows-runtime parity. Extended
arithmetic comparisons were run on Linux x86_64, not ARM hardware.

## Helicopter and Dragon navigation integration

Helicopter and Dragon both use ordinary subtype0x1d with radius30. Their World
regression in `ordinary_heli_ai_world_test` begins an actual targeting search,
then arranges a synthetic pending-search graph with a moved head minimum and
retained route endpoints. Both Seeker and Tracker must find the intervening
Thing body and must not admit the obstructed two-point direct route. Actor-cell
arrival ordering, motion centers and graph bounds are synchronized; normal
snapshot validators remain enabled. Each mode checks30 exact memory/disk
continuation ticks. Reverting only source-local traversal makes the blocker
assertion fail; the accepted movement and zero-route heading fixes remain intact.

The original40e630/40f9b0/40fce0 instruction probe reproduces the same boundary
history. Of12 focused cases, the old global-prefix scan differs in4 and the
source-local query matches all12. Replaying the exact exported World graph with
self and target disabled makes both original axis queries identify the blocker.
An additional460 radius30 graph-wave/order/selection records and13,122 query
controls match the repaired helper. These are synthetic instruction and port
World checks; they do not measure natural campaign frequency, original Windows
runtime behavior, or Helicopter/Dragon performance.

## Insertion pivot history (2026-10-24)

Fresh axes retain the original head/tail/pivot and signed balance semantics.
40e360 appends above the tail; otherwise it scans from the pivot when the new
coordinate exceeds the pivot coordinate, or from the head. After each endpoint
birth40e100/40e150 maintain the pivot. Deletion40e1a0 runs before unlinking;
ordinary adjacent repairs exchange pivot identity with crossed nodes, while
hinted40ea10 performs deletion notification, detach, rebalance, anchored insertion,
insertion notification and adjacent repair. Box birth remains maxX,maxY,minX,minY.
The index pool and derived coordinate cache remain in use.

The pivot is historical state, not necessarily the current middle endpoint.
A finite integer counterexample starts with bounds0..100 and zero-extent bodies
at4,6,8,10. Moving the first body to20..120 leaves an unordered head minimum.
Native insertion puts a route point9 after the8 endpoints; the previous port
head-prefix shortcut put it before20. A9-to11 query then missed body10 in the
port while the original found it. This difference requires no extended-precision
arithmetic: the comparisons use exactly representable coordinates. It can change
obstacle selection and subsequent navigation/contact decisions; natural campaign
frequency and visible symptoms have not been established.

`native_graph_pivot_test` checks355 deterministic mixed original-instruction
histories, including points, bounds, ordinary/hinted moves, ties, deletion,
empty/single-node transitions and slot reuse. Both complete endpoint lists,
binary32 coordinates, pivot identity and signed balance enter the fingerprint.
It also asserts the reproduced consumer query. These are synthetic instruction
and port controls, not an original Windows playthrough or FPS measurement.
Writer36 checkpoints retain the exact policy, optional pivot endpoint and signed
balance after each axis list. Readers1–35 keep their graphs under the previous
port insertion policy until a fresh graph is built; their missing pivot history
is not guessed. Invalid pivot membership or legacy metadata is rejected before
session replacement. `save_file_test` covers native birth/delete continuation,
nonmedian pivot preservation, actual v35 migration and transactional rejection.
