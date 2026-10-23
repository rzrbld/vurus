# XTC delayed speech

XTC queues one voice request when its pending pickup phase ends and the effect
is applied. It uses the existing World voice-event path and audio scheduler.
Music mode switching stays independent.

Original apply routine `0041c390`, instructions `0041c647–0041c67e`, reads
`World+1020 & 3`. Jump table `0041ca7c` maps residues 0/1 to sample slot `0x23`
(35 decimal, `V-ARIBA.WAV`) and 2/3 to `0x28` (40 decimal, `V-YEEPEE.WAV`).
The call to `00413200` supplies delay 3000 ms, hold 2000 ms and tail 0 ms.
The port selects from its existing elapsed World clock at application time;
this introduces no RNG draw.

The existing scheduler suppresses duplicate samples and makes one attempt when
a request becomes due. Competing speech can reject that attempt during its hold
interval. The delay therefore describes scheduling, not guaranteed audible
playback. Active XTC updates do not request the line again. In-memory checkpoints
retain the application guard; the existing disk-save normalization removes
Surprise state, and pending speech is not newly serialized.

Tests cover all four clock residues, the pending/application boundary, the
emitted timing tuple through the scheduler, no repeats after active memory
restore, disk normalization, and a live Item pickup using the authored XTC token.
Local original-resource checks exercise both clips in English and French through
GameAudio and the SDL dummy mixer. Those checks establish playback admission,
not original Windows listening or exact host-clock parity.
