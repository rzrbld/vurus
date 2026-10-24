# Menu confirmations and application exit

Evidence refers to the supplied original executable, SHA-256
`415f40822f0475808caa019122c1d3dd15da6c5e94ebceeffddc4b3a6134bc46`.
Static disassembly is corroborated by controlled execution of the original
input handler421780 and volume callback45e4e0; this is not a Windows playthrough.

## Exit and End Game confirmations

Application Exit4277c0 uses `Exit Now ?`, `Yes, bye.`, `No !`. End Game4223e0
uses `End this Game ?`, `Yes`, `No`. Both put the affirmative choice first.
Confirmation builder4228a0 draws the title at(10,120) with font2 and choices at
parent.left+30 (normally300), first baseline120, then165.

The descriptor field+0x50 is the selected-row exit-effect trigger. The menu
loop4211e4 compares the returned row against it and invokes421460 when equal.
Field+0x5c is the Escape/Backspace row, dispatched at421e15. Exit passes0/0;
End Game passes-1/0; Retry passes-1/1. Therefore Escape affirms both Exit and
End Game, while Retry's Escape selects its negative row. The earlier inference
that End Game's-1 argument meant Escape cancellation was incorrect.

The port matches these two confirmations' wording, order/default, placement,
font and Escape/Backspace dispatch. Other confirmations retain their existing
policy and layout. End Game returns to the title; application Exit closes after
its presentation.

## Exit explosion presentation

421460 runs inside the menu handler before returning to the application loop.
It draws MENU4 then screen-space `Explosion` and `Small-Explosion` resource
animations in creation order. Each rounded menu update samples a batch0..5
while attempted count<400; the final batch may reach404. Births sample x64..576,
y32..448 and variant0..1 in that order. Each admitted update advances existing
animations using the frame delta. Completed flags0x18 retire after drawing.
The effect ends after the burst drains, after10000ms, or on new keyboard/mouse
input. The port also permits controller buttons to skip. These are presentation
animations, never World actors or damaging explosions. Gameplay RNG is untouched;
the existing transient menu RNG supplies the presentation.

The authored small variant uses0x100 size scaling and0x4 diffuse fading. The
later447b50 ordering-screen entry is an immediate RET; that disabled screen
is separate from the earlier active explosion effect.

The retained executable writes source.z9999 before later animation updates;
its default spatial callback rejects that height (original45e4e0 execution:
volume0 atz9999,65535 atz0). EXPLO.GAF places samples after the first frame.
The user explicitly requests an audible blast: the port plays those embedded
samples on their authored frame entries through the unpositioned menu route,
all16channels/priority127, independent of World pause. This is an intentional
audible adaptation; original Windows listening parity is not claimed. Shared
sample-bank selection follows [the existing bank rules](animation_sample_banks.md).

The port-only [Debug Commander](../testing_debug_menu.md) presentation and
main-menu-style top-level Options spacing are independent of these confirmations.

## Audio options (2026-10-25)

Audio uses the original Music and combined Sound FX, Voices hierarchy, separate
0–100% Tune Volume pages and English/French Voice type. Disabled sources gate
their volume/type entries. Volume pages retain five blank rows and Back, with
a 20-segment text slider; Escape/Back returns to the invoking row. The accepted
32-bit/48 kHz output choices remain, and Advanced contains the added Linux mixer
controls. Original callbacks and scoped App checks are recorded in
`work/quirk-audit/audio-menu-native-20261024/README.md`; the
[audio pipeline contract](audio_pipeline.md) describes control behavior.
