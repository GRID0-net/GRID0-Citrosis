# Android memory and frame-time changes, October 18, 2026

The supplied Splatoon 3 log ends with Android `LOW_MEMORY`, at 4778 MiB RSS.
The last samples reached 4991 MiB RSS and 33 MiB MemAvailable. RSS includes shared
guest aliases, so it is not a measurement of unique physical memory. Both anonymous
and shared resident mappings grew rapidly. The renderer loaded 16,489 pipelines.
This establishes system memory exhaustion, but does not attribute every allocation.

## Changes

- **Fix shader pool destruction.** `Chunk::Release()` destroyed `Storage`, whose union
  destructor is empty, instead of the active `T`. Instruction phi arguments, IR block
  predecessor/successor vectors, flow block stacks and indirect branch vectors could
  retain their heap allocations after every translation. It now destroys each active
  object. The regression test fails on the previous implementation.
- **Bound retained translation storage.** With Fit caches to RAM enabled, release IR
  after creating the pipeline and retain at most 8 MiB per pool, except for the minimum
  initial chunk. Oversized pools return to the initial chunk rather than allocating a
  replacement as large as the previous shader. Small shader storage remains reusable.
  Failed constructors no longer leave unconstructed entries registered for destruction.
  Pipeline constructors copy shader metadata before this release; asynchronous driver
  compilation does not use the IR objects.
- **Earlier memory pressure response.** Moderate pressure begins below the larger of
  2 GiB or one quarter of physical RAM; critical begins below the larger of 1536 MiB
  or one sixth. Existing recovery hysteresis remains. Performance mode can purge free
  native allocations at critical pressure every two seconds rather than waiting until
  less than 512 MiB remains, then waiting ten seconds between attempts.
- **Faster eviction under pressure.** Texture and buffer collection can visit entries
  unused for 20 frames at moderate pressure and 6 at critical pressure. Scan budgets
  increase under pressure. Normal mobile texture ages become 90/30 frames, compared
  with 120/60. Deferred destruction and in-flight GPU resource lifetimes remain in place.
- **Bound readback stalls.** Mobile texture and buffer GC each read back at most one
  dirty resource per pass, continuing to scan for clean eviction candidates. This trades
  slower dirty-resource reclamation for fewer consecutive GPU waits. GC still runs on
  subsequent frames while above its threshold.
- **Staging reclamation before waiting.** Count upload, download and unmapped
  device-local staging capacity, including the stream ring. Use the configured staging
  target rather than overriding it with a 192 MiB floor. Reclaim all completed entries
  first. Wait at the frame boundary only if remaining staging exceeds twice the target,
  or the target itself at critical pressure. Consumer-owned deferred downloads remain
  protected, and cannot by themselves trigger repeated GPU waits. These are reclamation
  thresholds, not hard allocation caps: a single frame
  or deferred download can still exceed them.

The shader destructor fix applies to all backends. Bounded pool retention and the
mobile GC/staging policies depend on Fit caches to RAM. Game speed, swap intervals,
resolution and frame generation settings are unchanged. These changes do not establish
a measured 60 FPS gameplay / 30 FPS plaza result.

## Verification

The standalone regression covers object lifetime, pointer stability across pool growth,
bounded retention, trimming an already merged root, repeated small shader reuse,
destruction on exit, and pressure budget ordering across 4–16 GiB devices.

```sh
g++ -std=c++20 -Wall -Wextra -Werror \
  -fsanitize=address,undefined -fno-omit-frame-pointer -g -Isrc \
  src/tests/memory_regressions.cpp src/common/memory_budget.cpp \
  -o /tmp/citrosis-memory-regressions
ASAN_OPTIONS=detect_leaks=0 /tmp/citrosis-memory-regressions
```

AddressSanitizer and UndefinedBehaviorSanitizer pass. LeakSanitizer must be disabled
in this execution environment because its process tracing prevents LSan from running;
explicit lifetime counters still verify destruction. The test is also registered with
CTest as `memory_regressions`.

No Android APK build or device benchmark was possible on this host: the SDK/NDK are
absent and the installed Java is 25, while the Android build needs 17 or 21. Renderer
template compilation and GPU lifetime behavior still require the Android build/device.

For comparison, use the same driver, shader cache, save, resolution and graphics settings.
Repeat boot to plaza, a match, return to plaza, then several match/plaza transitions.
Compare memory growth over transitions as well as initial peak, pressure logs, frame
times and visible ink/shadow correctness. Test warm and cold shader caches separately.
The old log also contains `Unimplemented format=0` assertions shortly before termination;
this patch does not establish their cause or fix them. Driver pipeline memory, guest
working sets and allocations inside a single shader can still exhaust RAM.
