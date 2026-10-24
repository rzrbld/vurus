# Native manual high/low speed

Evidence: supplied PE SHA-256
415f40822f0475808caa019122c1d3dd15da6c5e94ebceeffddc4b3a6134bc46,
retained/fresh disassembly and isolated original-instruction controls. This is not
an original Windows playthrough. Detailed commands/hooks are under ignored
work/quirk-audit/manual-speed-audio-20261024/.

Action9 calls43b830 once per press edge. Active Player+d4==7 blocks the call;
that field is assigned only when delayed Thrust Loss applies, so pending Slow
still permits a toggle. Keyboard profiles1/3 use DirectInput0xd1/Page Down;
profile2 uses a joystick token. The port appends a remappable Page Down action,
controller unbound, leaving older action identities/bindings intact.

The helper compares live float speed threshold Player+1a4 with class+64.
If live<class, copy authored speed/turn period(class+7c) and queue HIGH-SPEED;
otherwise store float32(classSpeed*float32(1/3)) and truncate current signed
turn interval Player+ec times1.5, then queue LOW-SPEED. Both voice tuples are
0/1000/2000ms. Velocity, acceleration, thrust and existing turn countdown are
untouched; both logical movement and48-pose facing use the new interval.
1,485 finite controls/2,970 original helper calls match; only413200 is hooked,
original ftol executes. Invalid overflow/corrupt-state behavior is not emulated.

This is mutable state, not a low-mode boolean. XTC snapshots/doubles the live
threshold and expiry restores its snapshot without restoring the turn interval.
For class45/18: low15/27 →XTC30/27 →toggle45/18 →expiry15/18. Slow similarly
retains/restores a quarter threshold but blocks toggles while applied. State is
created lazily on first manual input; prior ordinary/Surprise paths remain.
Class baselines are cached separately from placement overrides. All25shipped maps
have Player class/placement45/45 and18/18, independently censused.

HUD4355cb..4355ef compares the same live threshold with the class threshold:
inequality selects PANEL4, equality PANEL5. Pinball elasticity is not this
predicate; Pinball movement/collisions/FX remain unchanged by the indicator fix.

## Scene and save lifecycle

Accepted Lift442bf0 copies Destination+24/+28 into World2098/209c. Session4467d0
retains a living Player when both are nonnegative, repositions/rebinds that same
body on reentry without resetting+1a4/+ec. Missing Destination defaults0,0:
405210 clears the complete0xa4attribute, Lift constructor409d00 adds no negative
default, and413580 copies all0x29dwords. World startup−1 is overwritten by the
accepted Lift. Thus normal campaign exits retain the manual values; explicit
negative/custom reconstruction resets them. These fields do not mean arrival XY.
Existing inventory/vitals handoff and authored arrival coordinates are unchanged.
Original arbitrary class/identity aliases remain outside this scoped integration.

Schema38 persists the explicit state; readers1–37 remain supported. In-memory
snapshots preserve XTC/Slow baselines; disk saving normalizes temporary effects
under existing port policy. Deterministic threshold persistence is a port save
guarantee beyond the inspected original text writer, which emits modified
Turn-timeout but omits the live threshold. See [checkpoints](audio_controls.md#manual-highlow-speed-and-checkpoints).
