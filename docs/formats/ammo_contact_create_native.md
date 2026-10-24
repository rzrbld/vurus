# Terminal Ammo CREATE ordering and origins


Terminal Ammo callers have distinct CREATE schedule owners, origin fields and
damage phases. The integration preserves the CREATE scheduler, selected contact,
rebound response and existing save fields outside the body-clock extension.

Target executable SHA-256: `415f40822f0475808caa019122c1d3dd15da6c5e94ebceeffddc4b3a6134bc46`.
`43d7d0` first updates the contact counters, then conditionally calls `437b00`
(source flags at `+0x128`, bit `0x04000000`). Only afterward does it install the
contact surface/cell, apply radial damage (`43cb20`), run Hit-fx/direct handling
(`43d350`) and remove the source (`437d40`). The actor-to-Ammo caller at `441e08`
uses this same sequence. A freshly created non-Ammo body may therefore receive
the same contact's radial damage. CREATE children remain independent and
ownerless; Hit-fx/Explosion ancestry follows its separate producer contracts.

Fuse or ground termination takes the distinct `440ebe` branch: radial damage,
CREATE, Hit-fx, destruction. A child born there is absent from that earlier
radial traversal. It is incorrect to put CREATE unconditionally at either end
of a shared impact helper.

CREATE coordinates are surface coordinates. The moving caller refreshes the
previous surface (`+0x64/+0x68`) from its retained pre-move native center at
`441797/4417aa`, then publishes the proposed surface at `4417ea/4417ff` before
contact handling. At-current-position must therefore see the proposed surface,
not the later contact point. Ordinary moving default emission sees the projected
old native center. An actor consuming stationary Ammo does not refresh that
Ammo's history; default emission uses the Ammo's retained previous surface.
Fuse/ground exits before the movement refresh and also retains that previous
surface. Grenade/Saw terrain rebounds preserve their distinct rollback and
response path rather than being forced to the terminal proposed position.

Emitted Ammo owns its transferred `Projectile::creates` schedule. Authored Ammo
without an emitted adapter retains `ActorState::creates`; the contact caller
must select the real owner rather than servicing an empty temporary schedule.
One-shot completion prevents duplicate births; repeating schedules still allow
one attempt per native visit. Failed construction leaves the timer due. No
per-frame catch-up rule or full Actor allocation for ordinary bare Ammo is added.


`contact_create_world_test` exercises the live producers, moving and retained origins, radial/birth order, failed factories and disk continuation. Original instruction probes hook construction/damage; they are not a Windows playthrough or whole-campaign parity certificate.
