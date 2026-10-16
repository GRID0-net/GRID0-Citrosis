# GRID0-citron networking migration audit (2026-10-15)

Compared Citrosis Android directly with GRID0-citron origin/main
8194d1295f9651477b7ce148813179b99c2fce3f. The donor SSL, NIFM and BSD
sources inspected have no local differences from that revision. This audit uses
the matching emulator family as the primary reference.

## Concrete omitted behavior, now ported

**ssl:s certificate export was empty.** Lemon already registered command 2
(GetCertificates) and command 3 (GetCertificateBufSize), so a command-table-only
comparison missed the problem: both functions took no inputs, returned only
ResultSuccess, and supplied none of the declared outputs. The ordinary ssl port
had the real CertStore implementation, including GRID0+ CA injection, but ssl:s
never called it. Loading 127 firmware certificates in the log therefore did not
prove the guest received any certificates.

GRID0-citron explicitly documents Splatoon loading roots via ssl:s immediately
after its first NPLN DNS result, before creating a BSD socket. This is a strong
source-based match for the current log (successful DNS and eventfd wake, no
socket creation), but the log does not expose those debug-level ssl:s calls.
The source defect is confirmed; its responsibility for this run's 2321-4992 is
not yet proven on device.

Ported upstream's real buffer-size/certificate commands and the adjacent normal
and system context creation paths. Both CreateContext and CreateContextForSystem
now return actual IPC objects. Ported the system context command surface, including
connection creation. The TLS backend remains Lemon's existing implementation.
Both service ports share one firmware CertStore to avoid a second copy in RAM.
Corrected upstream's GetThreadCoreMask reply to five words (Result + u64 + u32)
and the ordinary ssl command-table duplicate 8 to command 9.

**NIFM request readiness was only refreshed during guest calls.** Upstream
rechecks host connectivity every 100 ms through CoreTiming and signals both
request events on transitions, specifically because Splatoon's NPLN readiness
worker can wait without making another IRequest call. Citrosis already signaled
both events, but its state could remain stale after the initial Submit. Ported
the periodic refresh and synchronized state access, retaining airplane mode.
Unscheduling waits for callbacks before closing events. There are no added host
worker threads. GetResult now returns disabled when disconnected, rather than
returning success through the old Invalid/default branch. Corrected GetAppletInfo's
reply declaration to five words for its actual outputs.

## Where Splatoon interacts with this implementation

| Stage | Source | Migration assessment |
| --- | --- | --- |
| Build-specific guest changes | core/file_sys/private_server_patches.* and bundled IPS files | All six patches already applied in the supplied log; updated smallmatch is present. |
| Account identity and token | service/acc/acc.cpp, switchnet_account.*, async_context.* | Existing port; supplied log confirms successful token delivery, not game-server authentication. |
| Network readiness and events | service/nifm/nifm.cpp, IRequest | Missing periodic refresh ported in this change. |
| Initial content/news cache | service/bcat/backend/grid0_backend.*, service_creator.* | Existing port; current log confirms 24 cached files and zero failures. |
| Private DNS and gRPC wakeups | service/sockets/sfdnsres.*, private_server.*, deferred_poll_waker.*, bsd.cpp | Already ported, with prior Citrosis ABI/race corrections. Current log reaches successful DNS completion. |
| Guest-managed TLS roots | service/ssl/ssl.cpp, cert_store.* | CA injection existed, but ssl:s export was stubbed; corrected here. Splatoon can perform TLS itself over BSD, so absence of ISslConnection calls alone is not a TLS failure. |
| TCP/gRPC I/O and Pia transport | service/sockets/bsd.cpp and internal_network sockets | TCP_NODELAY, IP_TTL/TOS, IPv4-only creation, multi-message send/receive already ported. |
| Friends and async completion | service/friend/friend.cpp, grid0_friends.* | Existing friend methods match donor after IPC API adaptation, including completion signaling. |
| Network error play reports | service/prepo/prepo.cpp | Donor prints raw msgpack for diagnostics; Lemon uses real report handlers and its Reporter. This diagnostic difference is not a missing networking operation. Raw report dumping was not added. |

The normalized BSD comparison confirms RecvMMsg, SendMMsg and socket option
implementations match the donor after BSD/BSD_USA naming and IPC API adaptation.
Remaining differences include the intentional prior socket ABI, eventfd, polling,
nonblocking flags, deferred wake and diagnostic corrections. Upstream consumes an
eventfd counter during poll; Citrosis correctly leaves consumption to Read. That
behavior was retained, not reverted by copying the donor.

## Validation boundary

Source and response-layout review plus git diff --check only. No compilation,
tests, game launch, server modification or network probes were performed. The
next device log should expose ssl:s GetCertificateBufSize/GetCertificates at Info
level and show whether BSD socket creation now follows DNS. A successful token or
DNS reply alone does not prove a successful NPLN connection.

Memory budgets, reclamation, renderer/HDR safeguards, NCE and game IPS binaries
were not changed by this audit. Existing bounded guest tracing remains available
if the newly restored service behavior does not resolve the failure.
