# FakeRanks - Reveal All (Linux)

Linux x86_64 fork of [Cruze03/FakeRanks-RevealAll](https://github.com/Cruze03/FakeRanks-RevealAll), for Metamod:Source **2.0 prereleases**.

This plugin sends `CCSUsrMsg_ServerRankRevealAll` to the player opening the scoreboard. It displays ranks assigned by another plugin; it does not fetch real matchmaking ranks or assign ranks itself.

## Fix in 1.1.4

Upstream commit `ab9cda8` switched to KHook but only constructed the virtual hooks. Neither `KHook::Virtual::Add()` nor an equivalent registration was called. Consequently `GameFrame` and `StartupServer` never reached the plugin.

- Attach hooks after the engine interfaces are acquired; detach them on unload.
- Read scoreboard state every frame instead of every twelfth tick, so short observed presses are not discarded by polling.
- Reset button history on map shutdown/start, unavailable players, and pause/resume.
- Validate the rank-reveal message at load time instead of dereferencing a missing descriptor.
- Count all recipient slots rather than only the first 64 bits; mark the message as having no predicted player.

The scoreboard mask remains `1ULL << 33`. Entity fields are resolved by name through the game's schema system, rather than fixed pawn/button offsets. The Linux `GameEntitySystem` service offset remains **80**, matching [CounterStrikeSharp gamedata](https://github.com/roflmuffin/CounterStrikeSharp/blob/751eb0c8c0f83b566fb4de869c1a6fffdb7a008c/configs/addons/counterstrikesharp/gamedata/gamedata.json#L229). No new byte signatures were needed for this fix. The [signature tracker](https://github.com/ianlucas/cs2-signatures) was checked; the current [CS2 SDK site](https://www.cs2-sdk.com/) dump is Windows (build 14183), so its addresses are not used as Linux offsets.

## Install

Download a `FakeRanks-RevealAll-<version>-linux.tar.gz` from [Releases](https://github.com/koiie111/FakeRanks-RevealAll/releases) and extract it into `game/csgo/`. The package layout matches upstream:

```text
addons/metamod/fakeranks.vdf
addons/fakeranks/bin/linuxsteamrt64/fakeranks.so
```

Use Metamod 2.0 matching the release notes or newer, and a separate plugin that assigns scoreboard ranks. Restart the server and check `meta list` / `meta info fakeranks`.

## Automatic builds

[GitHub Actions](https://github.com/koiie111/FakeRanks-RevealAll/actions) builds on pushes to `main`, `v*` tags, pull requests, and manual dispatch. Only Linux x86_64 is built, using Steam Runtime Sniper and Clang 16.

Every six hours, the workflow checks the newest published Metamod **2.0 prerelease**. It checks out that exact release tag, builds against current `hl2sdk` (`cs2`) and manifests, and publishes a prerelease archive. The automatic tag includes the plugin commit and Metamod version. A scheduled run skips a combination with an existing archive; a failed build is retried on the next run. Pull requests produce artifacts without publishing releases. `build-info.txt` records all dependency commits separately from the installation archive.

External repository releases cannot directly trigger this repository's workflow, so detection uses polling and may be delayed by GitHub's scheduler. On a new fork, enable Actions. GitHub may disable scheduled workflows in public repositories after 60 days without repository activity; re-enable the workflow if that occurs.

## Build locally

Clone the plugin and these sibling directories: `mmsource-2.0` (the desired 2.0 release tag, recursive submodules), `hl2sdk-cs2` (branch `cs2`), `hl2sdk-manifests`, and `ambuild`. Install AMBuild with `python3 setup.py install`. In a Steam Runtime Sniper SDK container with Clang 16:

```sh
export CC=clang-16 CXX=clang++-16
mkdir build && cd build
python3 ../configure.py --enable-optimize --symbol-files --targets x86_64 --sdks cs2 --hl2sdk-manifests /absolute/path/to/hl2sdk-manifests
ambuild
```

The installation files are in `build/package/`.

## Validation

The fork was compiled in a Linux Docker container with Metamod `2.0.0.1469` and HL2SDK `6315f0104d22eb9ea3c33d0505dbe14e8b193bc3`. A successful build is not an in-game test. Validate on a CS2 server with a connected client and a rank-setting plugin:

1. Open/close TAB repeatedly, including brief presses. Only the requesting client should receive the reveal message.
2. Check living players, dead players and spectators with a valid controlled pawn.
3. Change map, reconnect, pause/unpause, and unload/reload the plugin; TAB should still work, with no stale callbacks or crashes.
4. Check logs for schema lookup failures after future CS2 updates. The fixed service offset and SDK interfaces can still require updates even when automatic compilation succeeds.

Original code credits: Cruze and Pisex (LR-FakeRanks / ServerPlayersListFix); included SDK helpers are derived from CS2Fixes. GPL-3.0.
