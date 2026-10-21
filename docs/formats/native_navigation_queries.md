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
intersection.40fce0/40ff00 transpose the axes. Do not round the quotient before
calculating the intercept, or round the multiplication before the final sum.
The implementation calculates the two coefficients once per axis scan.

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
