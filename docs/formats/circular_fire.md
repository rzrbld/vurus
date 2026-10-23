# Circular Fire slope boundary

Circular Fire retains the selected weapon angle during a pending sweep. Its
ordinary initiating or rearming input visit still updates the angle normally;
activating the effect does not choose a different starting angle.

In the retained executable, pending dispatch at `0x440dd7` / `0x440f1b` calls
`0x443bd0` directly, skipping input handler `0x438540` and slope helpers
`0x42c210` / `0x42c280`. `WeaponFireVisit::PendingSweep` preserves this boundary.
Ordinary held input advances Gatling-II from its authored initial −33° toward
horizontal in 0.5° steps; ordinary release resets it. Pending shots preserve
both the selected slope and the weapon's held state.

This correction does not add a release-to-rearm latch, change simulation timing,
set a universal projectile radius or alter ammo accounting. Disk saves continue
to remove temporary Surprise effects; no save layout changes are needed.

Regression coverage exercises fresh/warmed slopes, tap/held input, cooldown,
paired barrels, mid-sweep memory restoration and normalized disk restoration.
Original-resource checks confirm 24 Gatling-II volleys retain the chosen angle;
ordinary firing trajectories remain unchanged. These checks do not establish
complete original weapon or physical-input parity.


## Ammo charging and completion

Original `443bd0` receives a charge argument: the ordinary initial visit passes1,
pending visits pass0 until the final counter1 visit passes1. With Circular's
World flag8 set, a call charges once across its enabled barrels. Once the effect
expires, a retained pending sweep still finishes, but the cleared shared-charge
flag makes its final visit charge each product.

The port plans its enabled barrels separately from real stock consumption and
charges after actual projectile construction. A zero count runs the existing
compatible-ammo/loaded-weapon selection immediately. The current call retains
its original stock identity, so replacement ammunition is used on a subsequent
visit. When no replacement exists, the pending count clears without the
successful heading/pose step. Already-created bullets remain alive.

This follows `44433a`/`444344` → `444450` → `443cbe`, which bypasses the success
tail at `444390`. For a single stock, two enabled barrels, active Circular and
start heading9, starting rounds1/2/3 produce1/47/48 bullets and finish at
headings9/8/9. These expectations are derived from native control flow; local
World regressions cover them alongside compatible-stock/weapon switching,
cooldown, expiry, visible-body construction, slopes and save continuation.
There is no save-schema or ordinary/Triple firing-policy change. Original
allocation-failure paths and full Windows runtime parity are not established
by these finite controls.
