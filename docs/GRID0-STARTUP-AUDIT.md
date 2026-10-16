# GRID0+ Splatoon 3 startup audit — 2026-10-15

## Finding and scope

The user confirms the same online-at-boot hang occurred in GRID0-ryujinx and was
fixed there. The replacement NPLN is the GRID0+ server implementation, not a
replacement guest client in a missing emulator branch. This makes the existing
Ryujinx startup workaround directly relevant. It does not prove that every
Citrosis occurrence has the same underlying scheduling cause.

Reference source: GRID0-net/GRID0-ryujinx commit
`1b0d770555ce2e7c763d5e5b5a81ffd506bae65d`, read-only clone at
`/private/tmp/citrosis-grid0-ryujinx-reference`.

The Citron-to-Eden migration does not inherently prevent GRID0+ operation. It
requires matching the guest IPC, eventfd, BSD and executable-patch contracts.
Some defects below are shared with the old Citron checkout, so attributing all
of them to Eden would be incorrect.

## What the supplied log establishes

- Main build `28C4287AEE36F7499DA60F3E68B54C70DA382D75` has six selected built-in
  GRID0+ patches. The previous source revision adds post-success 6/6 reporting;
  the current log predates that reporting. Selection alone is not runtime proof
  of each patched instruction being executed.
- Emulator-side GRID0+ login succeeds, independently of the guest connection.
- Splatoon requests EnsureIdTokenCacheAsync at 12.413 seconds.
- At 12.418 seconds its NPLN host resolves to 89.168.58.206:443. The complete
  87-byte response matches readback from the actual guest C buffer. Command 12
  reports size=87 and zero resolver, host and BSD errors.
- There is no subsequent socket-creation trace, connect or TLS trace before
  periodic eventfd polls. This places the observed stall before the guest's
  transport reaches the server, assuming the trace is complete. It does not
  identify the guest thread or exact wait responsible.
- The process remains around 2.3 GiB RSS with substantial available memory;
  this particular run is not evidence of an OOM kill.

A replacement server can fix NPLN protocol behavior after connection. It cannot
answer an HTTP/2 request the client never opens a socket to send. The emulator's
successful native login also does not establish guest token delivery.

## Missing Ryujinx workaround now adapted

`DnsMitmResolver.MaybeAwaitJitSettling` delays the first NPLN DNS request by a
2-second floor, then samples JIT translation activity every 500 ms until calm
or a 60-second cap. Its source comments explicitly identify the online-at-boot
infinite-loading symptom and startup worker starvation.

Citrosis previously returned the first NPLN lookup immediately. It now holds
Splatoon 3's first private-server NPLN getaddrinfo request for 2500 ms, matching
the earliest release of the Ryujinx gate. Both getaddrinfo command variants use
it. Other games, offline/no-private-server operation and subsequent lookups do
not acquire another delay.

This is a deliberately limited adaptation: NCE does not expose Ryujinx's JIT
translation counter. A fixed minimum window is not equivalent to the reference's
adaptive wait during a sustained compilation burst, and its efficacy on the
Snapdragon 888 is still unverified. Do not describe this candidate as a proven
hang fix. If it releases successfully but still produces no socket, guest
thread/wait tracing is the next diagnostic; increasing a delay indefinitely is
not a diagnosis.

The request uses ServerManager's existing deferred-IPC path. The existing BSD
wake thread gains a deadline wake, so no service worker sleeps and no extra
thread is allocated. The timer is armed after the request is inserted into the
server's deferred list, avoiding a lost wake if scheduling runs past the deadline.
A deadline expires even if the originating session goes
away; it does not leave a permanent poll waiter behind. Auto-selected resolver
inputs are snapshotted per request, bounded to three 4096-byte buffers, so a
retry does not re-read pointer-buffer storage another guest thread reused.
Oversized input bypasses the delay rather than allocating an unbounded copy.

## Additional concrete source corrections

1. Eventfd's Horizon nonblocking bit is 4, close-on-exec is 2. The previous
   flags-first parameter-layout correction is confirmed by Ryujinx. Flags=6
   happened to set both bits, so correcting the bit alone cannot explain this
   supplied run's hang.
2. BSD socket type carries close-on-exec and nonblocking in bits 28 and 29.
   Decode them before type checks, accept close-on-exec without corrupting the
   type, and apply nonblocking to both the host socket and GETFL state. Clean
   up the descriptor if nonblocking setup fails. This removes a real transport
   compatibility defect, but the current log does not reach socket creation.
3. ResponseBuilder counted normal output words twice in HIPC raw-data size.
   Count them once and copy the declared section including zeroed tail padding.
   Only include a domain header when one is actually emitted.
4. Domain-object output offsets were calculated by dividing a word count by
   sizeof(u32) again. They now point immediately after normal output words.
   Example: with four normal words and one object, payload starts at word 10;
   the old code put the ID at word 13, overwriting normal output. It belongs at
   word 14. The account request's common two-word/one-object shape happened to
   land correctly at word 12 before this fix; do not call that object a proven
   blocker. The first commentary overstated its account-specific implication;
   that was corrected during the analysis.
5. Account async GetResult returned only a Result but advertised an additional
   output word. It now uses the two words required for the 64-bit CMIF result.
6. Bounded default-level account traces show domain mode, completion-event
   retrieval, HasDone, GetResult and actual token-copy completion, without
   exposing token contents. Native login success and guest token delivery can
   now be distinguished.

## Comparisons that do not justify further changes

- Ryujinx command-12 completion field order matches Citrosis.
- DNS IPv4 address length is 16 in both serialized headers. Ryujinx's sockaddr
  sin_len is 4, while Citrosis uses 16; these are different fields. libnx's
  deserializer sets sin_len itself. That discrepancy alone does not establish
  another DNS defect, so the current address bytes are preserved.
- Ryujinx's lost-address connect fallback can recover a 0.0.0.0/:: target from
  its most recent DNS answer. The current log has no socket or connect call,
  so copying that fallback does not address the observed stopping point.
- The server's assigned-peer readiness/small-match changes act after connection
  and lobby setup. They are distinct from this pre-socket startup gate.
- No NCE scheduling/fastmem changes, memory-budget relaxations, HDR changes or
  server deployment were made in this audit.

## Next-run evidence and limits

Expected new markers are `NPLN startup gate: deferring`, followed approximately
2.5 seconds later by `released`, then DNS completion and (if startup progresses)
BSD socket creation. Account async and token-copy markers identify the parallel
authentication path. Patch application should report its named 6/6 summary.
If there is a socket followed by a failure, distinguish connect errno, TLS
handshake and first HTTP/2/server response before changing another layer.

Only source review and whitespace checking were performed. No builds, tests,
SDK operations, emulator launches, device runs or server mutations were run.

Primary references:
- https://github.com/GRID0-net/GRID0-ryujinx/blob/1b0d770555ce2e7c763d5e5b5a81ffd506bae65d/src/Ryujinx.HLE/HOS/Services/Sockets/Sfdnsres/Proxy/DnsMitmResolver.cs
- https://github.com/GRID0-net/GRID0-ryujinx/blob/1b0d770555ce2e7c763d5e5b5a81ffd506bae65d/src/Ryujinx.HLE/HOS/Services/Sockets/Bsd/IClient.cs
- https://github.com/GRID0-net/GRID0-ryujinx/blob/1b0d770555ce2e7c763d5e5b5a81ffd506bae65d/src/Ryujinx.HLE/HOS/Ipc/IpcMessage.cs
- https://github.com/switchbrew/libnx/blob/master/nx/source/runtime/resolver.c

## 2026-10-15: new lobby error log — concrete transport rejection

The replacement /Users/lizzy/citrosis_log.txt is 150133 bytes and reaches the
plaza/lobby. It proves the startup gate deferred then released, all six exact-build
patches applied (including the 1035-byte small-match update), account async completed
and an 878-byte token reached the game. DNS redirects to 89.168.58.206 correctly.

At 155.757 and 155.759 seconds, ConnectImpl rejects two 16-byte IPv4 addresses
with errno 22 before invoking the host socket. The inherited Lemon SockAddrIn had
248 bytes of padding and sizeof=256, while the original Citron donor uses 8 bytes
of padding and a 16-byte address. The migration's exact-size guard therefore
rejected valid addresses. This is a proven local transport blocker, not evidence
that the GRID0+ server is down. These particular connections follow the
Service-status-lp1.cdn.nintendo.net lookup; the log does not prove the same calls
are the first NPLN gRPC connection or that this is the only remaining failure.

Corrected the wire struct to 16 bytes with size/port/IP offset assertions. Shared
IPv4 decoding for bind/connect/sendto validates family and minimum length and
accepts an IPv4 prefix in a larger storage buffer. IPv6 is still rejected rather
than misread. Accept/getpeername/getsockname/recvfrom now use the corrected size;
short output capacities are respected. Connection traces show host-order ports
and distinguish an ordinary nonblocking connect-in-progress result.

The log also has hundreds of poll(nfds=0, timeout=1..5) calls immediately returning
-1 with errno 0. Empty polls now use the existing deferred-poll deadline and return
zero on expiry, without reading nonexistent descriptor buffers or blocking a
service worker. Negative counts/timeouts are rejected. The plain poll fd upper
bound now excludes fd==MAX_FD.

News/cache path: Ryujinx prefetches GRID0+ BCAT at application load. This port had
a sync backend but only used it for an explicit guest sync. Added an initial
cache prefetch before handing out BCAT/cache interfaces, once per consecutive
title per creator. It uses the existing HTTP/hash-checking cache backend on the
BCAT worker, with no new thread. Initial prefetch stops scheduling additional
files after a soft 20-second budget; in-flight HTTP requests retain their existing
timeouts, so this is not a hard 20-second wall-clock guarantee. An offline or
unavailable server preserves the existing cache, and explicit guest sync remains
available. Native API failures now log method/path and transport or HTTP status,
without authorization headers or token contents. This supplies a missing cache
fetch path; news appearing in-game remains unverified.

Texture-cache unregistration assertions and low available-memory samples also
appear in this log; renderer/NCE and RAM policies are unchanged by this networking
work. No builds, tests, device launches, network probes or server mutations were
performed. Source/whitespace review only. Next evidence should show native
Connect attempting the intended endpoint, pending/success/failure and initial BCAT
manifest/download results, rather than rejecting a valid 16-byte address locally.

## 2026-10-15: 99578-byte follow-up log — no guest transport attempt

Latest user log: /Users/lizzy/citrosis_log.txt.old.txt, supplied after the closing
crash. The filename does not make this run stale. At 15.86 s native GRID0+ login
succeeds; the BCAT code reaches its initial-sync budget at 35.73 s. Reaching that
branch establishes that a valid BCAT manifest was fetched and parsed. At 36.63
and 57.21 s the account async request completes and an 878-byte token is copied
to the guest. All six exact-build IPS patches were accepted.

The first NPLN lookup is held until 39.127 s and resolves successfully; its full
87-byte C-buffer response matches readback. Guest eventfd write/poll/read wakeups
complete. The lobby retry at 57.217 s repeats DNS successfully and closes its
eventfd roughly 6 ms later. There are no New socket, socket(), Connect, TLS or
Send calls anywhere in this run. The old 16-byte-address refusal does not recur,
but this run does not exercise that corrected connect path at all. It cannot
establish a new TCP/TLS/server-side rejection or prove that the server is down.

### Narrow DNS wire-profile parity candidate

Compared the actual serialized fields with GRID0-ryujinx's IResolver,
AddrInfoSerializedHeader, AddrInfo4 and AddrInfoSerialized at
1b0d770555ce2e7c763d5e5b5a81ffd506bae65d. Its successful reference path emits:

| Field | Previous Citrosis NPLN reply | Working Ryujinx profile now used |
| --- | --- | --- |
| ai_flags | 1 (echoed request hint) | 0 |
| ai_socktype | 1 | 0 (Any) |
| ai_protocol | 6 | 0 (Any) |
| ai_addrlen | 16 | 16 |
| sockaddr sin_len byte | 16 | 4 |

The guest family, port/IP byte ordering, canonical hostname and terminal sentinel
are retained. Native DNS resolution still honors the stream/TCP hints to avoid
bionic supplying UDP entries. Only Splatoon 3's redirected .npln. endpoint uses
the Ryujinx wire profile; other games and ordinary DNS keep their prior reply.
The diagnostic now explicitly names wire_profile and wire fields, rather than
reporting native entry fields as if those were necessarily the wire response.

This is a source-grounded compatibility candidate at the observed stopping point,
not a demonstrated cause of 2321-4992. A matching guest buffer readback proves
what was written, not that Splatoon's resolver accepted it. If this candidate
still returns DNS success without a BSD socket request, guest resolver/thread
tracing is required; changing TLS or connect fallback cannot fix a call never
issued. The Ryujinx lost-address connect fallback remains unported for that reason.

### BCAT completion and connection reuse

The first implementation recreated an HTTPS/TLS client per file and marked the
initial prefetch done even when it returned false after the time budget. Added
a scoped RequestSession: one CA-verified keep-alive connection for the manifest
and sequential file requests, closed at the end of that sync, with no connection
pool or additional thread. Ordinary emulator API calls retain their one-request
wrapper. Endpoint/trust identity changes reset a session's connection. HTTP body
ownership is moved into the response, and cache writes consume that body directly
without allocating an additional whole-file vector.

The creator now marks prefetch complete only after success. A later BCAT/cache
open can resume incomplete work, skipping already matching files. Download,
manifest or file-write failures keep completion false. Added bounded first-file
progress logging and cached/downloaded/failed/byte counts plus the pending path
on budget exhaustion. The soft 20-second initial scheduling budget and per-HTTP
timeouts remain; one run is not guaranteed to finish all server content. These
changes address a real partial-cache condition, not proof that missing news was
the direct cause of the guest NPLN failure.

### Log hygiene and validation

The settings dump exposed the GRID0+ password. It is now excluded before settings
values are stringified, alongside the existing token exclusion. No credentials
were copied into this report, sent to a server or used for network probes.
Existing user log files were not modified.

No build, tests, device launch or server changes were performed. Source review and
git diff --check only. Memory budgets/reclamation, renderer/HDR and NCE remain
unchanged. The exit-time crash remains outside this online-connection investigation.

## Latest outcome: DNS parity candidate did not fix 2321-4992

The subsequent 115081-byte log confirms 24 BCAT files cached with no failures,
successful token delivery, the requested DNS wire profile/readback and eventfd
wakeups, but still no guest socket or connect call. Added bounded SVC/thread and
post-copy IPC tracing instead of further speculative network changes. See
GRID0-GUEST-TRACE.md for limits and interpretation. No build/device tests run.


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
