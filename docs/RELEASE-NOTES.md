# Citrosis desktop

Windows and Linux are back! this uses the same Citrosis core as Android, with the mix of Eden, Lemon and Citron-Neo changes and all the GRID0+ stuff brought over.

- GRID0+ gets its own menu for settings, friends, friend codes and patch checks.
- the Eden transfer wizard can bring over everything, or just configs and paths. replaced files get backed up.
- checking patches tells you when everything is already up to date.
- Windows includes the GRID0+ SSL fix.
- Linux AppImages include the fix for the Vulkan startup check launching outside the bundle.
- the Citrosis memory changes, performance options, frame generation and extra effects are here too.

## which file?

- **Citrosis-Windows-x86_64.zip** — extract it and open `citrosis.exe`.
- **Citrosis-Linux-x86_64.AppImage** — Intel/AMD Linux PCs and handhelds, including the Ally.
- **Citrosis-Linux-aarch64.AppImage** — ARM64 Linux.
- **Citrosis-source.tar.gz** — the matching source and pinned dependency sources.

on Linux, make the AppImage executable before running it. when updating Windows, keep your existing `user` folder. if an older build switched your graphics setting to OpenGL after the Vulkan warning, select Vulkan again.

bring your own keys, firmware, games and GRID0+ account. none are bundled. game performance depends on your system and settings; frame generation doesnt change the games actual simulation rate.

this is the desktop download set. Android builds are available through the Android workflow in the repo.
