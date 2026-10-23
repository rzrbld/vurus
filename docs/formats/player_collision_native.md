# Ordinary Player contacts

The ordinary Player now consumes the native graph contact selected for its
proposed body, rolls an admitted blocked attempt back, and uses the existing
native actor/terrain response helpers. This replaces the Player-specific swept
actor winner, partial advance to contact and centerline actor response.

The original target is `work/iso/Vyruz/vyruz.exe`, SHA-256
`415f40822f0475808caa019122c1d3dd15da6c5e94ebceeffddc4b3a6134bc46`.
Local instruction evidence is retained under
`work/quirk-audit/player-continuous-audit-20261023/` and repair validation under
`work/quirk-audit/player-collision-parity-20261023/`; private original-game
material is not bundled in the source distribution.
These are controlled executions and synthetic World tests, not a Windows
playthrough or a claim of complete campaign parity.

## Original call boundary

`4406e0` constructs one proposed native displacement, projects/clamps the Player
surface center (`441884..441960`), and invokes `4105d0`. Shape dispatch retains
native graph ordering, Item priority and later equal-distance replacement.
Circle/circle tests the proposed endpoint with the native minus-two margin;
terrain edges also have complete edge crossing through `407360`. Describing
all original collisions as endpoint-only is incorrect.

For an admitted blocked contact, `4449f0` restores the surface projected from
the saved native center before response. `436ac0` distinguishes actual previous
surface movement from a stationary commanded visit; the latter uses heading
and thrust as effective velocity. Actor response `436dd0` uses independent
source/contact and target/contact axes, separate elasticities and mobile-body
write ownership. The Player caller supplies absolute native float coordinates,
retaining their rounding boundary. Terrain response uses `4367d0` and the
existing native heading admission. Neither response refreshes cached magnitude
`+148`; the next propulsion decision still reads that cache.

The existing Item collection and saving sensors keep ownership of their effects.
A selected nonsolid Item does not become ram damage or a second proximity search.
Physical Items remain contact candidates. Touch still defers native death to the
victim visit. Existing actor-to-Ammo dispatch and continuation remain in place.

## Protected adapters and limits

- The accepted fixed 60 Hz scheduler and canonical 14 ms velocity units remain.
  This does not migrate the original manager's variable frame-time conversion.
- Clear movement retains the accepted surface substep float additions. Those
  additions do not run a second collision query or replace the native winner.
  Native projection remains authoritative after a blocked/separating contact
  and at the native map-center clamp (one pixel inside the surface map bounds).
- Active Pinball retains its accepted swept containment and response adapter.
  Surface-only synthetic terrain without native decor retains its explicit
  legacy adapter. Neither is claimed migrated by this repair.
- Singular original contact points may be nonfinite. The port retains its
  finite checkpoint fallback rather than propagating NaN momentum.
- Existing native motion/history fields suffice. No save layout or schema
  change is introduced; supported legacy readers remain in place.

## Checks

`player_contact_native_test` contains 896 independently recorded original query
winners, including two graph link orders, Cop/Item combinations and degenerate
cases. An optional local original-record input also checks contact coordinates.
The existing actor/terrain helper tests retain their original-instruction
fixtures. Additional translated Player response controls check absolute float
coordinates under both retained x87 precision settings.

`player_collision_native_world_test` exercises the real World movement path:
competing actor winner, oblique actor response/write ownership, rollback,
complete wall crossing, stationary commanded admission, unchanged clear motion,
and isolated disk-save continuation. Drone cancellation, natural expiry and
disk normalization also restore the parked Player's native contact history,
so the next move cannot return to the old excursion coordinates. The accepted Pinball test separately
checks active containment, expiry, propulsion cache and in-memory/disk replay.
The repair report records full-suite and original-resource checks and their
fixture corrections.
