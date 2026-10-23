# Triple Fire origins and ghost drawing

Target: retained original `work/iso/Vyruz/vyruz.exe`, SHA256
`415f40822f0475808caa019122c1d3dd15da6c5e94ebceeffddc4b3a6134bc46`.
This investigation uses static original code/resources, not original-runtime observation.

## Native observations

- `431ae0`, retained `work/p13_render_actor.txt` lines 80–170, updates the
  copy distance at actor+114 and clamps to400. Its two side passes walk six
  body-direction nodes in opposite directions, then multiply distance by the
  **surface coordinate** float pairs at `50d440`. It truncates the resulting
  actor coordinates to integers for drawing. The final pass restores the
  original actor coordinates.
- `443bd0`, retained
  `work/quirk-audit/items/evidence/surprise-shot-emit.txt`, uses the same table
  and body-direction side nodes for projectile origins. This occurs inside
  each authored barrel iteration, in side1/side2/center order, before adding
  the ordinary start distance and barrel gap. It does not interpret the copy
  distance as a Weapons-gap value.
- Example: body heading3 and distance400 produce side origins
  `(50.5964,0)` and `(-50.5964,0)` surface pixels. Body heading9 produces
  `(0,25.29824)` and `(0,-25.29824)`. All projectiles retain the selected
  firing direction. The port's old barrel-unit substitution put some extra
  shots roughly ten times farther from the vehicle than these origins.
- Copy body passes set drawing operation5 (`431d09`). The software callback
  table at `521264` contains two callbacks per operation; operation5's
  ordinary callback is `4db310`. Retained raw instructions:
  `work/quirk-audit/triple-fire/evidence/blend5-4db310.asm`.
  `4db372–4db395` skips the keyed source color, masks source and destination
  with `0xf7df`, adds, then shifts right once: a quantized half blend.
- `433710` receives operation5 for the copy body but submits the muzzle
  channel through `45ec90` with its own authored operation. Ground shadow,
  reactor and headlight channels also retain their individual operations.
  The renderer loops over all copies before consuming the successful-fire
  state. Therefore all three vehicles have a muzzle flash for one volley.

## Implemented correction

`game/triple_fire.h` shares the exact existing surface table implementation
with `projectileStartOffset`. World firing now stores the extra origin
separately from authored barrel offsets; no accepted ordinary laser muzzle,
movement, projectile velocity, or barrel spacing changes are needed.
The renderer uses that same helper. Its former projected direction helper
was already numerically close; this change prevents the rendering and
projectile paths from diverging again.

Copy vehicle bodies now use half opacity. The OpenGL blend is a continuous
approximation of the native RGB565 mask/rounding, consistent with existing
blend adapters. Auxiliary channels retain their own blend operations. A
volley's fire revision is consumed only after its complete submission list,
so side flashes are no longer discarded after the first vehicle.

The existing fixed-update expansion/contraction adapter remains unchanged;
native expansion is draw-driven. Fractional surface placement also remains a
port presentation adapter rather than reintroducing native integer snapping.

## Verification

`triple_visual_audit DATA_DIR OUTPUT_DIR` loads original CAVE/T1C1 assets,
checks all24 headings' three projectile positions against the native side
origins, and performs framebuffer controls for half-opacity vehicle bodies
and both side muzzle flashes. `triple-body.ppm` and `triple-fire.ppm` retain
visual captures. This establishes port rendering and static-rule consistency;
it does not claim a side-by-side original executable capture.

## Emission order and stock exhaustion

The emitter starts copy selector2 at443eaa. It follows six +4 direction links
for that first lateral origin, then six +8 links for selector1, then emits the
center at selector0: body heading+6, body heading−6, center, within each enabled
barrel. All products retain the selected firing heading. This ordering is
independent of the renderer's copy submission order.

Construction precedes charging at444327. With active Triple's shared-charge
World flag, a successful call consumes one round across its copies and barrels.
The final round therefore creates the first lateral product, then stops when
the captured original stock becomes empty. Compatible-stock/weapon selection
runs immediately, but replacement stock is used on a subsequent call. Existing
products survive. Once Triple expires, a nonzero contraction distance can still
produce copies; those products are charged individually without the active
shared-charge flag.

Start-distance is evaluated before the copy loop at443e1f–443eaa. A randomized
forward offset is drawn once per enabled barrel and reused by that barrel's
three products. Copy expansion does not introduce extra forward-distance draws.

The World path uses post-construction charging for Triple and Circular, retaining
slot identity across actor-array growth and loaded selection. Ordinary firing,
copy spacing/blending, movement and saved layouts are unchanged. Dedicated World
regressions cover all24headings, final/stocked single and paired barrels,
replacement ammo/weapons, expiry, RNG grouping and memory/disk continuation.
These are finite port controls against static native instructions, not complete
original Windows-runtime parity.
