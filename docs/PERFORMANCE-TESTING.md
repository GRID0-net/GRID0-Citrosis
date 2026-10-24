# Dense-scene performance pass

This pass changes the shared renderer used by desktop and Android. It preserves
the game's timing, draw calls, shaders and resolution. The goal is less host CPU
work and fewer unnecessary Vulkan render-pass breaks.

## Changes

- Descriptor tables grow geometrically from the actual inclusive hardware limit,
  retaining their allocation when the table changes. Previously every table
  allocated at least 524289 descriptors. Guest descriptor memory is still read
  on every lookup; edits and invalidation are not skipped.
- Graphics pipeline transitions keep 16 recent pointers. A cold transition uses
  the existing global pipeline hash table, rather than growing an unbounded
  linear scan and duplicating large pipeline keys for each transition.
- Vulkan buffer uploads no longer request a second post-copy barrier. Inline
  copies already carry their own barriers. Reordered stream uploads already run
  in the upload command buffer with its transfer-write to all-commands barrier
  before the main command buffer. Keeping a render pass open across those
  reordered uploads avoids needless pass endings. OpenGL retains its previous
  post-upload handling.
- Each compute dispatch and its existing before/after barriers occupy one
  scheduler command instead of three. The barrier scopes and order are retained.
- Performance samples enter the log every five seconds: game FPS, emulation
  speed, and active system-frame wall time. These are not GPU timestamp timings.

The synchronization design follows the existing copy/submission dependencies;
read-after-write and write-after-write dependencies remain memory dependencies.
See the [Vulkan synchronization guide](https://docs.vulkan.org/guide/latest/synchronization_examples.html).

## Local checks

Build the optional allocation benchmark with:

```sh
cmake --build build-appimage-aarch64 --target descriptor_table_bench
./build-appimage-aarch64/bin/descriptor_table_bench
```

A Fedora Asahi ARM64 release build measured 16842792 bytes for the old policy
versus 8224 bytes for the new policy with 32-byte descriptors and limit 127.
The warmed 32-initialization sample took about 14 ms versus 0.005 ms; 1000 table
switches took about 1.4 ms versus 0.003 ms. These are isolated host measurements,
not Splatoon FPS gains or measurements from the Ally. Actual table limits vary.

Descriptor tests cover allocation growth/reuse, the highest inclusive index,
base changes, invalidation and guest edits. Transition tests cover recency,
eviction and recovery through the caller's global table.

The opt-in production Vulkan upload test checks GPU round trips through both
inline and reordered upload paths, without an additional post-copy barrier:

```sh
CITROSIS_TEST_VULKAN=1 ./build-appimage-aarch64/bin/tests '[uploads]'
```

It passes on Mesa's software Vulkan driver on the ARM64 build host. This is a
correctness check, not RADV/Adreno performance or graphics validation. The test
skips by default when a test runner has no Vulkan device.

## Game comparison

Use the same game update, save, settings, power mode and scene for both builds.
Warm the shader cache first. Keep frame generation disabled when measuring the
game's own frame rate. Compare the old and new builds in the same match location,
facing the dense map for at least 30 seconds each, then repeat facing a simpler
view. Include the plaza as a 30 FPS timing check.

Keep the new log and note which samples cover each view. If the dense view still
misses its target, repeat at a lower rendering resolution to help distinguish
pixel/GPU pressure from host CPU work. Proper CPU/GPU traces on the target device
are needed before attributing any remaining bottleneck. No sustained 60 FPS
result is established by the local benchmark or transfer test.

# Splatoon 3 60 FPS pass (October 20, 2026)

Target: hold 60 FPS in matches on the ROG Ally (x86_64, Dynarmic, RADV) and on
Snapdragon 888 Android. These changes keep the game's timing, resolution and
draw calls; no frame is skipped.

## Changes

- **Asynchronous presentation is on by default on desktop.** Without it, the GPU
  thread drained the Vulkan worker (`Scheduler::WaitWorker`), then blocked in
  `vkAcquireNextImageKHR` and `vkQueuePresentKHR` every frame. With FIFO or
  gamescope, that wait can cost most of a vblank of command-processing time,
  which turns a frame that is barely over budget into a missed one. The present
  thread now absorbs those waits. Android keeps its previous default (off).
  Configs that never changed the option follow the new default.
- **Splatoon 3 performance profile** (`title_performance_profiles`, on by
  default; **System → Built-in game performance profiles**). For title
  `0100C2500FC20000` only, and only for options left at their global value in
  that game's settings:
  - GPU accuracy runs at **Fast**. Accurate added synchronous GPU flushes on CPU
    invalidations, texture readbacks on CPU reads, fenced semaphore writes,
    precise occlusion queries, host conditional-rendering compares and safe DMA
    reads. The status-bar button shows the accuracy in effect; clicking it hands
    control back to the user for the rest of the session.
  - CPU accuracy **Auto** runs as **Unsafe** on Dynarmic (x86_64 and 16 KiB ARM
    hosts): reduced-error reciprocal/rsqrt estimates and inaccurate NaN handling
    on top of Auto's FMA unfusing and global-monitor bypass. NCE is unaffected.
  - Set GPU or CPU accuracy in Splatoon 3's per-game settings, or turn the
    profile off, to compare against the previous behaviour.
- **GPU sync request fast path.** `GPU::TickWork` runs after every draw and
  locked a mutex each time; it now returns immediately when every requested
  sync operation already ran. Requesters always queue a GPU tick afterwards.
- **Texture upload.** The 16-byte sector unswizzle (previously Android-only) is
  used on every platform. HDR non-finite scrubbing is branchless so x86 compilers
  vectorize it. Output was compared against the previous code for random
  R16G16B16A16F/R16F uploads: identical.
- **Bottleneck diagnostics.** The five-second `Performance` log line now adds the
  worst game frame, frames over 20 ms / 34 ms, and host CPU usage for the GPU,
  VulkanWorker, VulkanPresent and CPUCore threads (OS thread CPU clocks; nothing
  is timed on hot paths). A thread near 100% is the limiter: `GPU` = command
  processing, `VulkanWorker` = Vulkan recording/submission, `CPUCore_N` = guest
  code. No saturated thread while still under 60 means host-GPU or a wait.

## Validation

Changed translation units pass a clang 18 `-fsyntax-only` check against the
project's compile commands (x86_64 Linux configuration). The thread-usage sampler
reports ~99% for a busy thread and 0% for a sleeping one. Nothing here measured
Splatoon 3 FPS: send the new log lines from a match (same spot, dense view) so
the next pass can target whichever thread is saturated.

# Splatoon 3 heavy-map pass (October 24, 2026)

## What the ROG Ally log showed

Docked, 1x, RADV Phoenix (Z1 Extreme), Fast GPU accuracy, async presentation on.
Light scenes held 60.00. Heavy maps (Barnacle & Dime) settled at 54-58 FPS with
emulation speed still 100%, `worst_frame_ms` stuck at 24-27 ms and 5-46 frames
per 5 s over 20 ms. No host thread was saturated during the drops: GPU 47-56%,
VulkanWorker 17-20%, CPUCore_0..2 30-47%. Frame times sitting at ~25 ms
(three 8.33 ms periods of the 120 Hz panel) mean individual frames are just over
the 16.7 ms budget. Neither the guest CPU nor command processing is the limit, so
the remaining cost is host GPU time and/or the GPU thread and the host GPU running
one after the other instead of overlapping.

## Changes

- **Early submission (Splatoon 3 profile).** On desktop, recorded work reached
  the host queue only every 4096 draws, which in practice means at the frame's
  fence/present flush: the host GPU idled while the GPU thread recorded a frame,
  then the GPU thread waited on the host GPU. The Splatoon 3 profile now submits
  every 384 draws (`TitleProfileState::early_submit_draws`), so the host GPU
  executes the start of the frame while the rest is still being recorded. The
  split happens in `FlushWork` before a draw records any state, the same point
  Android already splits at (512 draws). Turning the profile off restores 4096.
- **Batched texture downloads.** `TextureCache::DownloadMemory` drained the queue
  (`Scheduler::Finish`) once per overlapping image; it now records every download
  and drains once, as `DownloadGpuModifiedImages` already did.
- **Host GPU and wait diagnostics.** The `Performance` line gains:
  - `host_gpu=[busy=% gpu_ms/frame= ...]` from a TOP/BOTTOM timestamp pair around
    every submission (128-slot query ring, harvested on the worker without
    waiting; a submission is left untimed rather than stalling when all slots are
    in flight). `submits/frame`, `early` (mid-frame submits) and `drains`
    (`Scheduler::Finish` calls) are counted too.
  - `waits_ms/frame=[gpu_thread present_frame present_thread worker other]`: time
    each role spent blocked on host GPU completion (timeline semaphore waits),
    and time the GPU thread spent waiting for a free presentation frame.

## Reading the next log

- `gpu_ms/frame` >= ~16 during drops: host GPU throughput is the limit. Next
  levers are GPU-side (lower resolution scale to confirm, then ASTC: this tree
  decodes ASTC to uncompressed RGBA8 on GPUs without native ASTC, 4-16x the
  sampling bandwidth of the original on a bandwidth-bound APU).
- `gpu_ms/frame` clearly below 16 but `gpu_thread` wait high: serialization;
  tune `early_submit_draws` (try 256).
- `present_frame` high: presentation backpressure (gamescope/swapchain image
  count), not rendering.
- `drains` in the dozens per frame: a readback path is serializing; find its
  caller.

## Validation

Changed translation units pass clang `-fsyntax-only` (x86_64 Linux, Vulkan
1.4 headers, VMA 3.x); remaining diagnostics are the existing fmt-version
mismatch in `common/logging.h` from the local check environment. No build, run
or FPS measurement was done.
