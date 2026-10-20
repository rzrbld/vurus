# Helicopter and Dragon route heading

Helicopter and Dragon use native subtype0x1d. When their target returns exactly
to the retained route's start, the close-target steering branch can produce
equal route endpoints. Original42caa0 updates action+64/+68 from the target's
native center after turning, then42c9d0 computes the direction from retained
action+5c/+60 and writes heading+78. The4075c0 zero/zero x87/__ftol path produces
heading0. The executable used for this evidence has SHA256
`415f40822f0475808caa019122c1d3dd15da6c5e94ebceeffddc4b3a6134bc46`.

The port preserves that result in Heli/Dragon route shortcuts. Previously it
stored sentinel24, skipping subsequent route-facing turns and potentially
changing unguided weapon-heading admission. The shared contact helper retains
its accepted zero-vector sentinel24; other enemy route headings are unaffected.
No movement constants, detection range, weapon timing or save layout change.

The World regression runs actual Seeker and Tracker pursuit, returns the player
to the retained route start, checks heading0 and later body turning, and verifies
120memory/disk continuation ticks. Controlled original-instruction comparison
covers19,968Heli records/239,616fields; this repair removes816zero-heading
differences. Those controls do not establish natural-campaign frequency, fully
diverged trajectories or a Windows gameplay comparison.
