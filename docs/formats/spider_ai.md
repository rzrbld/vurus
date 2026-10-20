# Spider steering and animation timing

Spider uses separate Seeker and Patrol actions. Original executable procedures
42c510 and42e400 identify their normal records as type7, modes2 and12 respectively.
The direct near-target test42ce40 accepts a supplied Seeker action. During Patrol,
its fallback list lookup accepts only type2/mode2 or type1/mode1, so it cannot
reuse the ordinary Spider type7 Seeker record. Applying that near/brake result to
Patrol caused repeated forward/brake changes when the player stood close between
the four firing headings. Patrol now retains its own route decision.

Seeker endpoint arrival uses its authored Min-distance; Patrol retains radius4.
Detection, search destination, near-distance and tracking read retained native
centers, while target-change checks retain separate surface samples. Spider graph
search uses original x87 corner/range precision. Relevant procedures:42cd30,
42da80,42d8f0,42ce40,42caa0 and40f310.

The application advances visible animation for each simulation slice. The draw
phase uses only the last slice interval, once, rather than summing catch-up slices
or replaying the interval on extra redraws. This follows43f07f–43fc42 ordering;
authored Spider Graphic-update-needed is0. AI commands, authored animation frame
durations, movement constants and save layout are unchanged by this clock fix.
No new animation debounce or distance threshold is introduced.

`spider_ai_world_test` covers18original-derived World decisions and420ticks of
byte-exact search/close-target disk continuation. The opt-in
`spider_animation_clock_audit DATA OUTPUT` uses supplied original resources to
check actual application redraws, continuous walking and catch-up timing. It
writes captures only to the requested output directory. Build it explicitly:

```sh
cmake --build build --target spider_animation_clock_audit -j4
SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy ./build/spider_animation_clock_audit /path/to/game-data /tmp/spider-clock
```

Original-instruction probes established the scoped lookup and clock differences;
user gameplay confirmed the reported close off-axis twitch was gone. This is not
a claim of complete original-game navigation/contact/combat parity. Persistent
blocked gait, other turn behavior and broader terrain escape remain separate
validation boundaries. Proprietary executable/resource evidence is not included.
