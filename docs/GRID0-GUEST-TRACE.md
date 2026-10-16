# GRID0+ guest-side connection tracing

## Current evidence: 2026-10-15, 115081-byte citrosis_log.txt

The previous DNS-profile candidate did not resolve the user's lobby error.
This run confirms:

- BCAT completes: four folders, 24 matching cached files, zero failed files.
  There are no new file downloads in this run, so this is not an A/B performance
  measurement of connection reuse.
- GRID0+ native login, guest account async completion and 878-byte token delivery
  succeed. All six game patches are accepted.
- The NPLN lookup succeeds twice. The log confirms the Ryujinx-compatible wire
  profile and full C-buffer readback. Eventfd writes, poll readiness and reads
  follow both replies.
- There is still no Socket/New socket, Connect or TLS call. The second attempt
  closes its eventfd about four milliseconds after DNS completion.

This does not establish a TCP, TLS or server-side rejection. Successful writes
and host-side DNS completion do not establish the guest's resolver return value.
Do not tune TLS, add lost-address connect fallback or make more DNS-field guesses
without observing the missing transition into guest connection setup.

## Instrumentation added

After a Splatoon redirected-NPLN DNS reply, a five-second window watches at most
8 guest thread IDs: the DNS requester and threads participating in its eventfd
read/write/poll/close path. Selected SVC entry/return logs are limited to 192
records per window, plus up to 24 final IPC reply records and one six-frame caller
snapshot per watched thread. No new worker, timer, unbounded queue or trace file
is introduced. After expiry, the trace returns to an atomic-load-only check.

The hook is at PhysicalCore's existing SVC boundary. It reads the live stopped
CPU context into stack storage and never writes registers, changes SVC results,
scheduling decisions or NCE instructions. Frame walking is limited to six aligned,
monotonically increasing frame records within 64 KiB of the current stack pointer.
Unsafe guest-memory reads avoid GPU flushing. No account/token payloads or guest
strings are logged. Only thread IDs, code/stack addresses, selected wait/handle
parameters and return registers are recorded.

`GRID0+ guest IPC reply` reads the actual TLS words after the service's outgoing
copy. DNS cmd 12 should show result=0:0 and output=0x57,0,0,0 for the 87-byte answer.
Eventfd read/write should show result=0:0 and output=8,0; poll typically returns
1,0 when readable. These records distinguish response construction from what was
actually copied back to guest TLS. Deferred requests do not log an outgoing reply.

## Reading the next log

- `GRID0+ guest trace epoch=...`: a new bounded trace window, with the DNS tid.
- `GRID0+ guest svc ... phase=enter/return`: calls from watched threads. The first
  DNS syscall can have a return-only record because tracing starts while it is
  already in progress. A wait returning after the five-second window may have
  no logged return; an unmatched entry alone does not prove a deadlock.
- `return_x0` is the raw return register, not universally a Result. Void SVCs such
  as SleepThread and SignalProcessWideKey have no Result. A timed wait returning
  ResultTimedOut is not automatically an error in the client.
- `GRID0+ guest callers`: return addresses for linking the wait/exit path to the
  game's module/code locations. These are raw addresses, not inferred symbols.

Relevant SVC IDs from this checkout's svc.h:

| ID | Operation |
| --- | --- |
| 0x08 / 0x09 | CreateThread / StartThread |
| 0x0a / 0x0b | ExitThread / SleepThread |
| 0x16 / 0x17 | CloseHandle / ResetSignal |
| 0x18 / 0x19 | WaitSynchronization / CancelSynchronization |
| 0x1a / 0x1b | ArbitrateLock / ArbitrateUnlock |
| 0x1c / 0x1d | WaitProcessWideKeyAtomic / SignalProcessWideKey |
| 0x21 / 0x22 | SendSyncRequest / SendSyncRequestWithUserBuffer |
| 0x34 / 0x35 | WaitForAddress / SignalToAddress |

`key` records a defined IPC handle or synchronization address when relevant;
`timeout_ns` records a defined sleep/wait timeout. `return_x1` is recorded only
for CreateThread's output handle or WaitSynchronization's output index.

## Validation boundary

Source/whitespace review only; no builds, tests, emulator/device runs or server
operations. This is a diagnostic candidate, not a claimed fix for 2321-4992.
Memory budgets/reclamation, HDR behavior and NCE execution instructions are
unchanged. The successful BCAT/cache and password-log fixes are retained.
