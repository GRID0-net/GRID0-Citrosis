# Citrosis Android

The Android edition of [Citrosis](https://github.com/GRID0-net/GRID0-citron), based
on [Lemon Project](https://github.com/Ghael-V/Lemon-Project) with Citrosis RAM safeguards, GRID0+ networking and Splatoon 3 fixes.

This source lives on **`codex/citrosis-android`** in the GRID0-citron repository.
The **`main` branch retains the desktop/multiplatform implementation**. Maintain
these trees separately; port shared fixes deliberately rather than merging this
Android source tree over main.

- [Build Citrosis Android](docs/BUILDING-CITROSIS-ANDROID.md)
- [Upstream provenance](citrosis-upstream.json)
- [GRID0+ networking audit](docs/GRID0-CITRON-NETWORK-AUDIT.md)
- [Project handoff](handoff.md)

The user confirmed the latest Android build works after the `ssl:s` and NIFM
networking fixes on 2026-10-16. This is user-reported validation; no independent
build or gameplay testing was performed by the agent preparing this publication.

The **Citrosis Android APK** GitHub Action builds this branch on manual request,
producing an arm64 Release APK, SHA-256 and source commit receipt. 
