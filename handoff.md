# Citrosis Android: Lemon migration handoff — October 15, 2026

The Android project now lives in `/Users/lizzy/GRID0-citrosis-android`, cloned from
https://github.com/Ghael-V/Lemon-Project at
`7fcd6ae60b74e36cce2eb06c0c39492bd636b8ec` on branch `codex/citrosis-android`.
The Citron Neo-based `/Users/lizzy/GRID0-citron` checkout is preserved, including its local changes.
This is a new downstream checkout, not an in-place Git-history rebase of that old tree.

Start with [the build/feature guide](docs/BUILDING-CITROSIS-ANDROID.md).
`citrosis-upstream.json` records the base/donor revisions and selected migration scope.
`citrosis-port-inventory.json` lists the core network files adapted during integration.

## Ported

- Citrosis mainline identity (`net.grid0.citrosis`), development signing key, Android lifecycle/JNI
  hooks, memory switches, Online account controls, login check and GRID0+ Friends UI.
- Physical-RAM/MemAvailable budgets, pressure monitor and trim callbacks, native heap pressure
  handling, sparse zero fills, freed guest page discard, coalesced optimized allocation, Android
  no-huge-page hints, pressure-aware texture/buffer GC, dirty-target retention outside emergencies,
  16 MiB staging ring, safe frame-boundary staging reclamation, smaller VMA blocks and bounded
  async decoding. No unverified mid-draw lifetime machinery from the old renderer.
- GRID0+ signed account token/ID, DNS redirects, configured/built-in CA trust, friends/presence,
  BCAT, exact-build Splatoon 3 toolbox IPS files, eventfd/deferred poll support and Pia options.
  Lemon's API changes were adapted explicitly: variant socket results, global room network,
  kernel arguments for event/progress signaling, FS naming, retained BSD helper services and
  neighbor-detection services. Lemon's existing DNS/TLS implementation remains underneath.
- LAN interface selection and broadcast steering, ConnectivityManager gateways and multicast lock.
- Splatoon 3 HDR float-upload sanitizer: exact non-finite masks and replacement counts,
  ARM64 NEON/scalar behavior, fused sanitize-before-Vulkan-write with bounded stack scratch.

## Upstream retained

Lemon's NCE/ARM64 machinery, shader/pipeline compilation, frame generation, BCn/Mali conversion,
savestate core and other renderer optimizations remain the base. No changes to `src/core/arm/nce`
or `src/core/memory.*` were imported. The recent NCE launch-regression experiments were excluded.
Nextendo has now been removed from native and Android account/routing/settings code.
GRID0+ is the integrated online provider. The upstream APK updater remains disabled.

The mainline flavor is the Citrosis build target. Other upstream flavors still exist; this port
has not separately adapted/tested their branding, preset defaults or install identities.

## Current evidence and next steps

All changes are local and uncommitted. Nothing was pushed. No APK was built and no compilation,
tests or device validation were run, as requested. Source review, include/reference inspection,
XML parsing, formatting and whitespace checks cannot establish buildability or runtime behavior.

Build it yourself with `./build-citrosis-android.sh --setup --jobs 6` (SDK command-line tools and
JDK required). Candidate output after your build: `build-android/Citrosis-release.apk`.
The wrapper prints the resulting hash; no APK hash exists yet.

Verify first launch before performance comparisons, then test HDR/ink, RAM survival, GRID0+
login/friends/BCAT/LAN and real multiplayer. Retest pressure transitions and staging under a
long match. Do not attribute any remaining panic or FPS result to NCE without the new run's evidence.

## Host Java selection correction

The first user build failed before native compilation with `What went wrong: 27`.
The Gradle 8.13 daemon log confirmed Java 27 from Homebrew. The wrapper now selects
JDK 17/21, searches versioned Homebrew paths even when macOS java_home cannot find
them, prints the chosen runtime, and pins org.gradle.java.home to that selection.
No build or device tests were run for this correction.

## Texture-cache header include correction

The user build reached native step 948/1740 and failed compiling vk_graphics_pipeline.cpp:
texture_cache.h referenced Common::MemoryBudget and CitrosisDiag without their headers.
Added direct common/memory_budget.h and video_core/citrosis_diag.h includes to the
template implementation header. A source scan found no other users of these namespaces
missing their direct includes. No build or device tests were run for this correction.

## Linear upload API correction

The next user build passed the texture-cache include fix, then failed in util.cpp:
UnswizzleImage retained a fourth diagnostic-label argument from the donor's
ReadBlockUnsafe API. Lemon accepts three arguments. Removed that label argument
from the fallback linear read, retaining the fused HDR and Android copy paths.
No build or device tests were run for this correction.

## Deferred socket poll wait API correction

The next user build linked video_core successfully, then failed in
deferred_poll_waker.cpp because Lemon has no Common::CondvarWait helper.
Use condition_variable_any::wait(lock, stop_token, predicate), matching Lemon's
existing queue/worker code and preserving stop-aware shutdown and idle sleeping.
The pasted log reports no other distinct compiler errors. No build or device tests
were run for this correction.

## Friends IPC response API correction

The user build reported five friend.cpp errors: Lemon's PushCopyObjects and
PushIpcInterface require HLERequestContext as the first argument. Added ctx to both
event handle responses and all three service-interface responses. Reviewed these
helper calls across modified sources; account and NIFM already pass ctx.
No compilation or device testing was run for this correction.

## Socket migration audit after the next user build

Corrected four root issues reported by the compiler:
- Removed duplicate IP/TCP SocketLevel enumerators.
- Added the guest BSD INET6 constant (28) used to reject unsupported IPv6 requests.
- Restored BSD_USA's is_user member and constructor initialization for DuplicateSocket.
- Implemented OfflineSocket::GetBytesAvailable (zero readable bytes), completing SocketBase.

Expanded source review to modified and added C++/Kotlin files: checked duplicate enum
names, stale CondvarWait/Citron path APIs, IPC response context arguments, socket class
overrides, BSD method declarations against definitions, project include paths, JNI entry
names/signatures, new Friends binding fields, Kotlin imports/value resources, and modified
XML parsing. No additional concrete compilation blockers were identified by those checks.
Added direct standard-library includes for the BSD header and JNI std::min use.
These source checks are not a compiler or linker; builds/tests remain user-owned.
RAM/HDR policies and upstream NCE were not changed by this correction.

## Missing BSD handler definitions at final native link

The user build compiled native sources and reached ld.lld, which reported 18 missing
BSD_USA handlers. The migration registered/decorated the donor handlers but omitted
the donor's trailing implementation block. Restored all 18 definitions from the
original Citrosis source, renamed BSD to BSD_USA, and added a direct core/memory.h
include for batched message memory access. This includes real SendMMsg/RecvMMsg
handling required by GRID0+ gRPC, rather than replacing those handlers with stubs.

The earlier audit checked definitions against declarations, but missed declarations
and registrations without definitions. The expanded source audit inspected 276
handler registrations across changed/added service sources; each has a visible
implementation. All 18 symbols listed in the user log now have definitions.
Whitespace checks pass. Native linking and Android packaging still require the
user's build; no compile/link/test command was run here.

## Android copy icon resource correction

The user's native library compiled and linked successfully. Android resource linking
then failed because fragment_grid0_friends.xml referenced ic_copy without the drawable.
Copied the original Citrosis ic_copy.xml vector into this checkout. Reviewed resource
references in changed/added XML: the copy drawable was the only missing app-owned
resource; remaining external references belong to Material/AppBar dependencies.
XML parsing and whitespace checks passed. No build or device tests were run here.

## Friends snapshot Kotlin nullability correction

The user build passed resource linking and reached Kotlin compilation. Six errors
in Grid0FriendsFragment.render came from assigning nullable data to snap after a
when statement whose returning null branches were not inferred by this compiler.
Use an explicit `val snap = data ?: return` guard before rendering the populated
Friends view. Other snapshot accesses already use safe calls or non-null results.
Whitespace checks passed; no build or device tests were run here.

## GRID0+, Friends and Citrosis branding follow-up

User evidence: /Users/lizzy/lemon_log.txt.old.txt. GRID0+ authentication succeeds,
and the NPLN hostname resolves to 89.168.58.206. The old DNS message says Nextendo
because of a shared label, not because it proves the alternate provider was active.
At 67 seconds, eventfd initval=6 is reported readable by poll and immediately read
as EAGAIN. PollOnce cleared the counter as a side effect. It now only reports
readiness; Read consumes the counter. This removes a definite lost-wakeup bug,
but the log does not prove the entire cause of the boot stall or server transport.
SocketExempt now delegates to Socket instead of returning EOPNOTSUPP (matching
libnx's same-argument socket command); host socket initialization errors propagate
instead of returning a descriptor backed by a failed socket. Retest on device.

Removed Nextendo account/session implementations, source registrations, native JNI
methods, Android sign-in/boot hooks, settings and resources. GRID0+ no longer has
a stale alternate-provider toggle that can suppress its account/DNS configuration.
Preserved generic redirected TLS SNI/ALPN support needed by GRID0+. GRID0+ account
resource cache now receives the authenticated account ID. Source attribution stays.

Friends launch previously called startActivity on the application Context without
FLAG_ACTIVITY_NEW_TASK. It now uses the existing actionSetting host Activity.
The standalone Friends Activity initializes theme and language like SettingsActivity.

Rebranded display text in 25 localized XML files, app flavor labels, new default
profiles, hard-coded FG/driver labels, native version logging and exported log names.
Resource IDs, package/JNI identifiers and genuine upstream URLs/copyright notices
remain compatible. Log producer/share/exit-reason paths consistently use
citrosis_log.txt and citrosis_gpu.log. Existing user-created profiles are untouched.

Validation: 33 modified XML files parse; modified Kotlin app-resource references
resolve; changed local project includes resolve; no functional Nextendo references
remain in changed code; git diff --check passes. No build, tests, emulator run or
network requests to GRID0+ were performed. RAM budgets/GC/HDR policies and NCE
were not changed. Rebuild, turn airplane mode off in global and Splatoon 3 per-game
settings, then retest online startup and Friends before gameplay/performance.

## DNS cleanup after provider removal

Removed the now-unused static IsHostOrSubdomain helper from sfdnsres.cpp. It was
left behind when Nextendo routing was removed, and -Werror=unused-function stopped
the user build. A source scan of modified C++ files found no other static function
helpers with only their definition remaining. Whitespace checks passed; no build
or device tests were run here.

## Remaining startup stall: resolver transport follow-up

Evidence: /Users/lizzy/citrosis_log.txt (432 lines). GRID0+ login succeeds and
Friends fetches four entries. The previous eventfd correction is confirmed by
read fd=1 -> 6 errno=0, instead of EAGAIN. At 95.48 seconds the NPLN name resolves
to the configured address twice, but no socket creation/connect/TLS request is
logged before subsequent five-second idle polls. This is not proof of a server
transport failure: the recorded stall is before the first connection attempt.

Compared resolver IPC and serialization to libnx's upstream resolver.c and
services/sfdnsres.c. The port ignored the guest's serialized socket-type/protocol
hints. It now decodes the 24-byte big-endian hints header, validates supported
IPv4/socket/protocol requests, accepts an empty sentinel, and forwards transport
selection into native getaddrinfo. Existing two-argument host calls keep default
behavior via default parameters. Host flags are not blindly passed across OS ABIs.

DNS output now rejects undersized output buffers and short writes instead of
reporting a full successful serialized response. Added GRID0+-only diagnostics
for IPC command, requested service, hints, returned IP/port/family/type/protocol,
capacity and written bytes. A 48-byte stack readback checks the first guest-visible
reply bytes; this retains no reply cache and records no account tokens/passwords.
Look for GRID0+ resolver request/entry/reply and readback_match in the next log.

This is a resolver correctness correction and a targeted diagnostic candidate,
not a confirmed full boot fix. Source checks found referenced enums/includes and
matching host resolver declarations; git diff --check passes. No build, tests,
network requests to GRID0+, or device run were performed. Memory budgets, staging
reclamation, HDR and NCE remain untouched.

References consulted: https://raw.githubusercontent.com/switchbrew/libnx/master/nx/source/runtime/resolver.c
and https://raw.githubusercontent.com/switchbrew/libnx/master/nx/source/services/sfdnsres.c.

## AutoSelect resolver readback correction (latest still-stalled run)

The latest /Users/lizzy/citrosis_log.txt confirms a stream/TCP DNS response for
89.168.58.206:443, cmd 12, hints flags=1/family=0/type=1/protocol=0, and 87 bytes
reported written. Eventfd Read continues to succeed. No socket connection is logged.

The new readback_match=false was a diagnostic error, not proof of a failed DNS
write: the readback inspected the zero-length B placeholder at address zero.
WriteBuffer actually selected C. Corrected the readback to mirror WriteBuffer's
B-if-nonzero-else-C selection, guarded zero addresses, and bounded it to 128 stack
bytes so this 87-byte response is checked completely. It now logs the actual
output descriptor/address, readable status and first magic bytes. The diagnostic
uses ReadBlockUnsafe to avoid adding a renderer readback side effect.

Compared cmd 12 output field order to Atmosphere-NX's DNS MITM implementation:
size, retval, host_error, errno is consistent. Added explicit completion-field
logging. Also preserve the requested ai_flags in the serialized response instead
of always emitting zero, matching Atmosphere's copied-hints behavior. Enable the
bounded BSD trace at RegisterClient rather than waiting for the first DNS redirect,
so initial eventfd setup before DNS is not lost; eventfd creation identifies the
BSD service to distinguish separate descriptor tables.

The infinite loading is not yet isolated or confirmed fixed. Do not treat the
previous false readback as evidence to alter NCE, guest mappings or RAM safeguards.
Whitespace/source checks only; no builds/tests or GRID0+ server requests run.
Reference: https://github.com/Atmosphere-NX/Atmosphere/blob/master/stratosphere/ams_mitm/source/dns_mitm/dnsmitm_resolver_impl.cpp

## EventFd CMIF layout correction after verified DNS reply

Latest /Users/lizzy/citrosis_log.txt proves all 87 DNS reply bytes match the
active C buffer, magic beefcafe, cmd 12 returns size=87/retval=0/host_error=0/errno=0.
The preceding DNS and readback diagnoses are no longer the observed blocker.

The trace repeatedly shows EventFd initval=6/flags=0 for gRPC's wake descriptors.
CMIF raw input parameters are stable-sorted by increasing alignment (Atmosphere's
RawDataOffsetCalculator), so EventFd's u32 flags precede the u64 initial counter:
flags at offset 0, four padding bytes, initval at offset 8, total 16 bytes.
The inherited parser read the counter first, treating flags=6 as initial count=6.
Replaced it with an explicit input struct and compile-time size/offset assertions.
Expected next-run eventfd trace: initval=0 flags=0x6. Initialized the descriptor's
O_NONBLOCK status from the nonblocking creation flag, retaining semaphore behavior
and non-consuming poll readiness. No guess-based initval=6 special case is used.

Audited neighboring mixed-width input: RegisterClient skipped no client-PID
placeholder and therefore logged the placeholder as transfer_memory_size. It now
skips the u64 placeholder before reading transfer-memory size, as libnx sends it.
Fixed BSD fd==MAX_FD bounds rejection to prevent an out-of-range table access.

This is a concrete ABI correction consistent with the trace; whether it clears
the complete boot stall remains device-unverified. No builds/tests or server calls
were run; whitespace checks passed. RAM reclamation, HDR and NCE are untouched.
Source: https://github.com/Atmosphere-NX/Atmosphere/blob/master/libraries/libstratosphere/include/stratosphere/sf/impl/sf_impl_command_serialization.hpp
(RawDataOffsetCalculator::StableSort and Offsets, alignment ordering).
RegisterClient reference: https://github.com/switchbrew/libnx/blob/master/nx/source/services/bsd.c

## Exact-build GRID0+ patch audit

Latest user log matches main build 28C4287AEE36F7499DA60F3E68B54C70DA382D75
and shows six built-in private-server patch attempts at 2.83–3.34 seconds.
All six local IPS files are byte-identical to the original GRID0-citron copies:
s3certpin_bypass, s3grpcpeer_bypass, s3grpcverify_bypass, s3moveless_bypass,
s3smallmatch_bypass and s3verifyoption_bypass. Static file parsing confirms valid
PATCH/EOF or IPS32/EEOF records. Both formats are supported by PatchIPS. The NSO
loader applies these before NCE code patching, with the expected NSO header prefix.

The old log printed Applying before checking PatchIPS success. Added category
lookup for embedded spans and named applied/FAILED messages after the result, plus
a per-module applied/selected summary. No patch bytes, offsets, selection rules,
CPU code or memory policy changed. Source/whitespace checks only; no build or
device tests run. The new summary should report 6/6 if every patch is accepted.
The latest eventfd trace now shows initval=0 flags=0x6 as expected, but startup still
stalls after a complete successful DNS reply with no socket connection logged.

## 2026-10-15: Ryujinx startup-path comparison and source candidate

Read GRID0-ryujinx at 1b0d770555ce2e7c763d5e5b5a81ffd506bae65d. The user
confirms its online-at-boot hang matched this symptom and was fixed there; their
replacement NPLN means the GRID0+ server implementation. Full findings, ranked
limits and source references are in docs/GRID0-STARTUP-AUDIT.md.

Adapted the missing first-NPLN DNS startup gate: 2500 ms once for Splatoon 3 with
a private server, deferred IPC, existing waker deadline, no added worker thread.
NCE has no Ryujinx JIT counter, so this retains the minimum window only, not the
adaptive up-to-60s JIT wait. Resolver input snapshots are bounded to 12 KiB per
request; no unbounded request copy or permanent wake waiter is added.

Corrected eventfd nonblock bit (4, not 2), BSD creation flags/nonblocking state,
shared response raw-word double counting, domain object placement and trailing
padding copy. The common account two-word/one-object reply happened to have the
right object placement previously; it is not a proven account hang cause.
Account async GetResult now advertises only its actual result words. Bounded
traces expose account async completion and successful guest token delivery.

No NCE, HDR or RAM-policy changes. No builds/tests/device/server operations;
source review and git diff --check only. This candidate is not runtime-proven.

## 2026-10-15: latest GRID0+ Toolbox 0.8.10 patch imported

Updated donor GRID0-citron from origin/main commit
8194d1295f9651477b7ce148813179b99c2fce3f, merging into android-port while
preserving Android commits and local edits. Copied its exact 1035-byte IPS32
s3smallmatch_bypass for build 28C4287AEE36F7499DA60F3E68B54C70DA382D75 into
this Lemon-based Citrosis Android tree. This is the assigned-peer roster/Pia
readiness gate, distinct from the boot DNS gate. Other five patch files, local
boot fixes, memory controls and HDR behavior remain unchanged. CMake already
tracks the IPS input, so the next normal build regenerates the embedded bytes.
Provenance/hash and upstream behavior are recorded in docs/GRID0-PATCH-UPDATES.md
and citrosis-upstream.json. Source/IPS review only; no builds/device tests.

## 2026-10-15: lobby 2321-4992 and missing news follow-up

New 150133-byte log proves boot gate release, 6/6 game patches, successful guest
account completion/token copy and DNS redirection. It then rejects valid 16-byte
IPv4 connect inputs locally (errno 22), because Lemon's SockAddrIn was 256 bytes.
Restored the real 16-byte wire layout with offset assertions, shared family-aware
IPv4 decoding and corrected address output lengths. Fixed the observed empty
poll busy loop: timer-only polls now defer and return zero after timeout.

Added the missing initial GRID0+ BCAT prefetch on service/cache open, analogous
to Ryujinx's startup prefetch. Existing HTTP/hash/cache code and worker are reused;
a soft 20-second initial-download scheduling budget leaves explicit sync intact.
API failures now log transport/HTTP status without tokens. News presentation and
full lobby login still require device validation. Detailed evidence/limits are
in docs/GRID0-STARTUP-AUDIT.md. RAM safeguards, HDR, NCE and renderer policies
remain unchanged. No builds/tests/device/server operations; whitespace review only.

## 2026-10-15: latest old.txt run still stops before socket creation

The new 99578-byte old.txt log proves native login, valid BCAT manifest handling,
6/6 game patches, guest token copies, successful DNS and working eventfd wakeups.
No socket/connect/TLS call occurs, including the immediate lobby retry. Therefore
the old IPv4-size fix is not exercised in this run; do not claim a TCP rejection.

Added a narrowly scoped Splatoon redirected-NPLN DNS profile matching working
GRID0-ryujinx: flags/type/protocol=0, addrlen=16, sockaddr sin_len=4. Native lookup
still filters stream hints. Logs show actual wire fields. This is a compatibility
candidate, not a confirmed explanation of 2321-4992; a continued pre-socket failure
requires guest resolver/thread tracing. Detailed comparison is in the audit.

BCAT timed out after its initial budget. It now reuses one scoped keep-alive HTTPS
connection per sync and avoids extra whole-file copies. Incomplete/failed prefetch
is not marked complete, so later cache opens can resume it. Progress counters
identify downloaded/cached/failed files and bytes. Password is omitted from future
configuration dumps; user logs remain untouched. No RAM/HDR/NCE policy changes.
No builds/tests/device/server operations; source and whitespace review only.

## 2026-10-15: DNS parity did not clear lobby error — bounded guest tracing

Latest citrosis_log.txt is 115081 bytes. BCAT now completes with 24 cached files
and zero failures. Tokens, six patches, DNS wire-profile/readback and eventfd
wakeups succeed. Still no guest Socket/Connect/TLS call; the retry closes its
eventfd within about four milliseconds. Do not claim the last DNS candidate fixed
the error or blame a TCP/TLS response never observed.

Added read-only tracing at the existing SVC boundary for at most eight DNS/eventfd
threads, five seconds after an NPLN reply, with 192 SVC records and 24 final IPC
TLS readbacks. Captures actual stopped CPU PC/LR, defined wait parameters, return
registers and bounded frame addresses. Logs include thread-creation failures and
final DNS/eventfd result words. No guest strings/token payloads or new workers.
No additional speculative connection behavior changes. Trace caveats, SVC IDs and
expected TLS words are in docs/GRID0-GUEST-TRACE.md. No builds/tests/device/server
operations; whitespace checks only. RAM/HDR policies and NCE instructions remain
unchanged. The complete cause of 2321-4992 is not established yet.


## 2026-10-15: Direct GRID0-citron audit found missing ssl:s behavior

The ordinary ssl port had real certificates, but ssl:s GetCertificateBufSize and
GetCertificates were empty success stubs inherited from Lemon. Upstream explicitly
identifies Splatoon's ssl:s root-loading immediately after NPLN DNS and before BSD
socket creation. Ported those certificate APIs, real normal/system context objects
and the system context service surface. Shared one CertStore across both ports.
Also ported upstream's 100-ms CoreTiming NIFM readiness refresh with synchronized
state access and preserved airplane mode. Corrected GetAppletInfo and system SSL
core-mask response sizes. Friends completion, multi-message I/O, socket options and
CA injection were already ported. Detailed source map, evidence and limits:
docs/GRID0-CITRON-NETWORK-AUDIT.md. Source review and diff whitespace check only;
no builds, tests or device runs. The source omissions are confirmed; resolving
2321-4992 remains unverified until the user's next build/run. RAM/HDR/NCE unchanged.


## 2026-10-16: Publication in GRID0-citron

The user confirmed the latest build works. The complete Lemon-based Android source
is being published to GRID0-net/GRID0-citron on codex/citrosis-android, keeping main
as the desktop/multiplatform tree. The Lemon origin remote remains available for
upstream comparisons; the grid0 remote is the publication target. Shared networking
fixes should be ported intentionally between implementations, not merged wholesale.
The separate main integration PR adds branch discovery and the manual Citrosis
Android APK workflow; it does not replace desktop source or release automation.
The action uses the existing wrapper and outputs APK/checksum/source provenance.
No build, test or workflow dispatch was performed during publication.
