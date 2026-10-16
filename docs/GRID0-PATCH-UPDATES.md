# GRID0+ patch updates

## 2026-10-15 — Toolbox 0.8.10 assigned-peer readiness gate

Source: GRID0-net/GRID0-citron commit
`8194d1295f9651477b7ce148813179b99c2fce3f` (latest fetched `origin/main`).

Updated the exact upstream `s3smallmatch_bypass` IPS32 file for Splatoon 3 11.3.0,
main build `28C4287AEE36F7499DA60F3E68B54C70DA382D75`.
The file grew from 261 to 1035 bytes and contains 61 records. SHA-256:
`a855cfec093a8c8f0cb441ceb46da44f8afbb07d293fba102f14ee2415310fa6`.

Upstream describes the patch as matching GRID0+ Toolbox 0.8.10: wait for a
completed assigned `All` peer-group roster and matching connected Pia count,
retain native reference ownership while inspecting the group, and preserve the
original stability counter. Upstream reports a successful Switch-hosted
three-player hotspot battle; that is not Citrosis Android device validation.

The other five exact-build IPS files are unchanged and match the donor. The
existing CMake embed generator watches patch-file contents, so the next normal
build regenerates the embedded patch bytes. No loader or patch-selection change
is needed; the patch count for this build remains six. Restart emulation after
installing the rebuilt APK to load the updated patch.

This updates match/peer readiness after connection. It is separate from the
first-NPLN-DNS boot-loading candidate documented in GRID0-STARTUP-AUDIT.md.
The local IPC/socket/startup-gate fixes, memory safeguards and HDR behavior are
preserved. No builds or runtime tests were run.

The donor `/Users/lizzy/GRID0-citron` merged `origin/main` into `android-port`
without replacing its Android commits or uncommitted changes. Its pre-merge
uncommitted diff is saved at `/private/tmp/grid0-citron-before-8194d1295.patch`.
The previous Android patch bytes are saved at
`/private/tmp/citrosis-smallmatch-before-toolbox-0.8.10.ips`.

Reference: https://github.com/GRID0-net/GRID0-citron/commit/8194d1295f9651477b7ce148813179b99c2fce3f
