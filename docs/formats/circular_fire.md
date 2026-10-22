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
