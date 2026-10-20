# Ordered GLES submission

The GLES compatibility shim retains emitted vertices until presentation or an
ordering barrier. It uploads a shared vertex stream, then submits commands in
their original order. Only adjacent commands with matching texture, blend
enable/factors, primitive, matrices and shader state may merge. Vertex tint
remains per vertex; shader and matrix arithmetic are unchanged.

Triangles and lines merge only when each command contains complete primitives.
Points may merge; strips, fans and loops remain separate commands. Texture
sorting across intervening commands is forbidden because sprites and shadows
depend on painter order and blending.

`gles_compat.h` redirects the engine's GLES resource/state entry points into the
shim. Texture bindings and blend factors are captured for each command; their
logical driver state is restored after a flush so existing queries still work.
Other driver state changes, resource mutation/deletion, framebuffer operations,
copies and pixel readbacks flush pending commands first. New raw GL operations
must follow the same rule when they can affect previously submitted geometry.
Calls inside the shim explicitly use the driver entry points to avoid recursion.

`Gfx::present()` flushes before presentation effects and before swapping. The
save-preview framebuffer and filtering readbacks use the same ordering barriers.
GLES thumbnail capture is requested by a save, not performed on ordinary frames.
The checkpoint snapshot is taken before rendering; the requested frame is copied
before swap and delivered to the background writer afterward. Cancelling a
pending preview releases the writer without invalidating the saved checkpoint.
The queue flushes at65536vertices or4096commands; a single larger draw follows
the immediate path after completing the queue. Storage is reused between frames.
Desktop compatibility OpenGL retains its existing submission path.

The renderer regression target checks translucent tint/order, nonadjacent A/B/A
textures, texture mutation/deletion, matrices, alpha/blend/scissor changes,
incomplete primitives, queue limits, filtering, brightness and save previews:

```sh
cmake --build build --target render_backend_test
SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy ./build/render_backend_test
```

Run the target in a GLES build as well. Software rendering checks correctness;
performance measurements require the target device. Actual H700 prototype
comparisons used640×480, uncapped/VSyncOff and alternating immediate/queued runs.
In a fixed crowded scene,249uploads became1 and249draws became191; median
render/present fell19.74->17.70ms. Live combat measured21.00->18.41ms.
These intervals include CPU submission, driver and presentation, not isolated
GPU execution. Different live frame durations also change the number of fixed
simulation ticks per frame, so total frame gains are not direct GPU savings.

GLES animation frames and font glyphs can share bounded atlas pages to reduce
texture changes between adjacent compatible draws. See
[atlas storage, filtering and residency rules](render_sprite_atlases.md).
