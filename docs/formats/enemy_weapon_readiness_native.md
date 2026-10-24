# Selected-barrel enemy readiness


The ordinary and Spider fire callers apply42d3df..42d409 before emission.
One-by-one (Weapon flags+30 bit8) bypasses Delay only when the raw active-Ammo
word at actor+12c equals zero. Otherwise compare unsigned32
`WorldClock - selectedBarrel.lastFire` against Weapon+1c Delay. Selection437680
starts the timestamp at zero, so a late birth does not wait Delay from its birth.
A declined fire bit retains the separate admitted/in-range state and reveal.
Player first-input policy is a separate accepted boundary.

Spider slope is advanced/released from42caa0 steering independently of this
readiness result. Scan-only visits retain it, and deferred emission must not
repeat the slope visit. Passing readiness as the slope trigger would reset a
Gatling during every cooldown; the live World regression now covers this case.

`enemy_weapon_readiness_test` compares30,000 retained original-instruction results;
`enemy_weapon_readiness_world_test` covers live native callers, raw wrap and disk
continuation. The optional authored-resource audit reproduces Cop first-fire
154ms for Delay150 under14ms steps; the previous caller fires42ms. These checks
do not establish whole-campaign behavior parity.
