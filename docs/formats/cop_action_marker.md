# Cop action marker retention

The accepted Cop preview retains its previous action marker when a controller
exits its route corridor before writing a new marker. This is a deliberate
Cop-only behavior choice, based on user gameplay preference. Seeker, Tracker
and Patrol preserve markers1,2 and12 respectively in the tested escape cases.
Random with a Seeker still receives marker1 if that Seeker runs before the
base action exits. Other ordinary enemy families keep their existing reset.

The full original game clears actor+194 before AI dispatch at441076 in4406e0.
The individual Seeker escape at42c6ff–42c71c leaves that field untouched, but
that does not mean the original retains the previous tick's marker. Earlier
analysis omitted the caller reset; cross-tick retention must not be described
as exact original-game behavior.

The marker selects route-motion correction and is already part of persisted
ordinary AI state. There is no layout/schema change. This choice adds no player
motion, environment change or lean animation; Cop lean sprite selection already
exists separately. A visible movement effect was not established by the
synthetic tests; acceptance came from the user's preview comparison.

`ordinary_cop_burner_ai_world_test` covers acquired-route exits for Cop Seeker,
Tracker and Patrol, ordering with Random/Seeker, an unchanged Burner control,
and sixty byte-exact memory/disk continuation ticks per case.
