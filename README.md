![chatterinoLogo](https://user-images.githubusercontent.com/41973452/272541622-52457e89-5f16-4c83-93e7-91866c25b606.png)
Chatterino7 [![GitHub Actions Build (Windows, Ubuntu, MacOS)](https://github.com/seventv/chatterino7/actions/workflows/build.yml/badge.svg?branch=chatterino7)](https://github.com/SevenTV/chatterino7/actions?query=workflow%3ABuild+branch%3Achatterino7) [![Chocolatey Package](https://img.shields.io/chocolatey/v/chatterino7?include_prereleases)](https://chocolatey.org/packages/chatterino7)
============

Bajerino is a fork of Chatterino7, but with useless features.

Chatterino7 is a fork of Chatterino 2. This fork mainly contains features that aren't accepted into Chatterino 2, most notably 7TV subscriber features.

### Features of Bajerino

- Message encryption: set an `Encryption Password` in settings, then use the per-channel encryption toggle in the split input to encrypt outgoing messages. Bajerino will also try to decrypt incoming messages with the same password.

- Anon Mode: the `Join Twitch IRC anonymously` setting reconnects Twitch IRC as an anonymous user while keeping your signed-in account for Helix API actions. This is useful if you want to read chat without exposing your logged-in IRC identity, but it forces chat sending over Helix and disables Twitch EventSub features.

- Selective Twitch proxying: `CHATTERINO2_PROXY_URL` is the proxy, applied to all Qt network traffic by default. `BAJERINO_PROXY_TWITCH=1` limits the proxy to Twitch connections (Helix, Twitch GraphQL, PubSub, EventSub, and IRC) while third-party services stay direct. `BAJERINO_PROXY_TWITCH_API_ONLY=1` narrows it further to only authenticated Twitch connections (Helix, Twitch GraphQL, PubSub); IRC and EventSub stay direct, so it pairs well with Anon Mode.

### Bajerino changes

A summary of notable Bajerino changes on top of Chatterino 7. The full history, including dev and upstream-merge commits, lives in the git log.

- Selective Twitch proxying via `CHATTERINO2_PROXY_URL`, `BAJERINO_PROXY_TWITCH`, and `BAJERINO_PROXY_TWITCH_API_ONLY` (see [Proxying](#proxying)).
- Anon Mode: join Twitch IRC as a logged-out user while keeping your signed-in account for API actions. (`6f85d48f`)
- Per-channel message encryption: a toggle in the split input, an encryption hotkey, and a decrypt indicator. Uses AES-CBC with a password, encoding ciphertext as CJK characters. (`1b4f806f`, `143e49d7`, `8264e58a`)
- Option to store settings in the Chatterino directory. (`d13986b8`)
- Bajerino branding: rename `chatterino` → `bajerino` in the GUI, a custom app icon, and custom badges. (`6cff983e`, `59365670`, `a4cf35e4`)
- Rebased onto Chatterino 7.

### Features of Chatterino7

- 7TV Name Paints

- 7TV Personal Emotes

- 7TV Animated Profile Avatars

- 4x Images (7TV and FFZ)

### Screenshots

![Example of Personal Emotes](https://user-images.githubusercontent.com/27637025/227032811-837c56eb-7724-431b-b00e-b944c9289dff.png)
![Example of Paints](https://user-images.githubusercontent.com/27637025/227034147-cb1fcd76-dbae-4878-9551-96ffa64dd1a9.png)

### Downloads

**Stable builds** can be downloaded from the [releases section](https://github.com/SevenTV/chatterino7/releases/latest).

To test new features, you can download the **nighly build** [here](https://github.com/SevenTV/chatterino7/releases/tag/nightly-build).

Windows users can install Chatterino7 [from Chocolatey](https://chocolatey.org/packages/chatterino7).

### Issues

If you have issues such as crashes or weird behaviour regarding 7TV features, report them [in the issue-section](https://github.com/SevenTV/chatterino7/issues). If you have issues with other features, please report them [in the upstream issue-section](https://github.com/Chatterino/chatterino2/issues).

### Discord

If you don't have a GitHub account and want to report issues or want to join the community you can join the official 7TV Discord using the link here: <https://discord.com/invite/7tv>.

### AVIF Support

When building Chatterino 7, you might not have access to a static build of `libavif`. In that case, you can define `CHATTERINO_NO_AVIF_PLUGIN` in CMake. If you have `qavif.so` from [kimageformats](https://invent.kde.org/frameworks/kimageformats) installed on your system, Chatterino will pick it up and use AVIF images.

### Proxying

If you set `CHATTERINO2_PROXY_URL`, Bajerino proxies all network traffic by default — Qt traffic via the Qt application proxy, and the asio-based connections (EventSub, PubSub, 7TV/BTTV/Kick live updates) explicitly.

Two environment variables narrow that scope:

- `BAJERINO_PROXY_TWITCH=1` proxies only Twitch connections — Helix, Twitch GraphQL, PubSub, EventSub, and IRC. Twitch images and third-party services (7TV, BTTV, Kick) stay direct.
- `BAJERINO_PROXY_TWITCH_API_ONLY=1` is narrower still: it proxies only authenticated Twitch connections — Helix, Twitch GraphQL, and PubSub. IRC and EventSub stay direct. (If both are set, this one wins.)

`BAJERINO_PROXY_TWITCH_API_ONLY` should be used in tandem with the `Join Twitch IRC anonymously` setting. It proxies your account/API traffic, while IRC and EventSub stay direct. If you enable it **without** anonymous IRC, your authenticated account still connects to Twitch IRC directly, unproxied — leaking exactly the authenticated connection you were trying to keep behind the proxy. With anonymous IRC enabled, IRC connects as a logged-out user (so there is nothing to leak) and EventSub does not connect at all. PubSub is included in the proxied set because, unlike EventSub, its subscriptions do not make your account appear as joined in chat.

## Original Chatterino 2 Readme

Chatterino 2 is a chat client for [Twitch.tv](https://twitch.tv).
The Chatterino 2 wiki can be found [here](https://wiki.chatterino.com).
Contribution guidelines can be found [here](https://wiki.chatterino.com/Contributing%20for%20Developers).

You might also need to install the [VC++ Redistributables](https://aka.ms/vs/17/release/vc_redist.x64.exe) from Microsoft if you do not have it installed already.
If you still receive an error about `MSVCR120.dll missing`, then you should install the [VC++ 2013 Restributable](https://download.microsoft.com/download/2/E/6/2E61CFA4-993B-4DD4-91DA-3737CD5CD6E3/vcredist_x64.exe).

## Building

To get source code with required submodules run:

```shell
git clone --recurse-submodules https://github.com/Chatterino/chatterino2.git
```

or

```shell
git clone https://github.com/Chatterino/chatterino2.git
cd chatterino2
git submodule update --init --recursive
```

- [Building on Windows](../master/BUILDING_ON_WINDOWS.md)
- [Building on Windows with vcpkg](../master/BUILDING_ON_WINDOWS_WITH_VCPKG.md)
- [Building on Linux](../master/BUILDING_ON_LINUX.md)
- [Building on macOS](../master/BUILDING_ON_MAC.md)
- [Building on FreeBSD](../master/BUILDING_ON_FREEBSD.md)

## Git blame

This project has big commits in the history which touch most files while only doing stylistic changes. To improve the output of git-blame, consider setting:

```shell
git config blame.ignoreRevsFile .git-blame-ignore-revs
```

This will ignore all revisions mentioned in the [`.git-blame-ignore-revs`
file](./.git-blame-ignore-revs). GitHub does this by default.

## Code style

The code is formatted using [clang-format](https://clang.llvm.org/docs/ClangFormat.html). Our configuration is found in the [.clang-format](.clang-format) file in the repository root directory.

For more contribution guidelines, take a look at [the wiki](https://wiki.chatterino.com/Contributing%20for%20Developers/).

## Doxygen

Doxygen is used to generate project information daily and is available [here](https://doxygen.chatterino.com).
