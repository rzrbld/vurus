# Legacy navigation scheduling

The generic legacy `NavigationSearch` adapter retains its original round-robin
cursor and shared budget. Native ordinary-enemy, Spider and Missile action
controllers keep their separate scheduling and search rules.

`GameWorld::legacyRouteSearchSeen_` is a conservative absence proof. Load/restore
examines current actor searches during the existing candidate-index rebuild.
Runtime search creation and common actor registration (including copied held
originals) set the flag when a search is present. Completion, cancellation,
death and slot reuse never clear it. Once set, the original full scheduler stays
enabled until the next load/restore rebuild. Compiler-generated World moves
carry the flag; it is not serialized.

When absence is proven, an AI-enabled full World pass skips the empty actor
sweep. It still adds the actor count to the saved unsigned navigation cursor:
the original loop incremented that cursor once per visited row even with no
searches. Unsigned wrap, AI-off admission and Time Warp player-only passes remain
unchanged. New searches start later in the actor traversal and become eligible
at the next scheduling boundary as before.

This changes no save schema, actor identity/lifetime, RNG consumption, native
search cadence, budget, graph ordering or gameplay rule. The flag can remain
true when no searches remain; that deliberately trades some potential savings
for simpler ownership and cancellation handling. A future search-creation path
must set it or use the common registration path before returning to the scheduler.

The regression covers empty and wrapped cursors, AI-off/Time Warp admission,
restored pending work, move/rebind, exact save continuation, cleared-search
restore and runtime legacy-search creation. Actual H700 testing used four
1,200-frame ABBA runs and exact synthetic/save/image controls. World median fell
from 5.450 to 5.152 ms in that crowded scene; worst spikes worsened, so this is
not a general stutter fix or an equal-work throughput claim.
