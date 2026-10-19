# Citrosis

this is a crazy amalgamation fork of Eden, Lemon and Citron-Neo, combining the great parts of each to be the go-to GRID0+ emu! its extremely performance oriented, and meant to be easy to use and easy to set up.

Windows, Linux and Android, all using the same Citrosis core. there are a bunch of fixes and changes mixed in here, with a big focus on memory usage, performance, and getting GRID0+ working without a ton of messing around.

## whats in here?

- **GRID0+ built in** — account settings, friends, friend-code search, requests and presence.
- **GRID0+ patch updates** — checks for the patches you need and lets you know when youre already up to date.
- **less RAM pressure** — memory limits and cleanup aimed at keeping heavier games running, especially on Android.
- **performance options** — shader compilation changes, frame generation, and extra graphics effects to play around with. results still depend on your device and the game.
- **easy Eden transfer on desktop** — bring over your full user folder, or just your configs and paths. replaced files get backed up and your Eden files stay where they are.
- **portable Windows builds and Linux AppImages** — grab the one for your system and get going.
- the usual game library, controller settings, per-game settings, save states, and the other stuff youd expect.

## getting started

[grab a release here](https://github.com/GRID0-net/GRID0-Citrosis/releases). on Windows, extract the whole zip and open `citrosis.exe`. on Linux, make the AppImage executable and run it. use `x86_64` for Intel/AMD PCs and handhelds like the Ally, or `aarch64` for ARM64 Linux.

already using Eden? open **Tools → Transfer from Eden…**. GRID0+ settings, friends and patch checks have their own **GRID0+** menu on desktop. on Android, youll find them in the apps settings.

bring your own keys, firmware and game dumps. none of those are included. GRID0+ also needs your own account and the right server settings.

## building it yourself

[build.md](build.md) has the commands for Windows, Linux and Android. the build workflows are there too if youd rather let GitHub do it.

## credits

huge thanks to [Eden](https://eden-emu.dev/), [Lemon Project](https://github.com/Ghael-V/Lemon-Project), Citron-Neo, and everyone whose work made this possible, including the original yuzu contributors. Citrosis carries their work forward alongside the GRID0+ changes; the original copyright and license notices are kept in the source.

Citrosis is free software. see [LICENSE.txt](LICENSE.txt) and [LICENSES](LICENSES) for the licenses.
