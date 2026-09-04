<p align="center">
  <img src="resources/icon.png" alt="Bajerino logo" width="128">
</p>

# Bajerino [![Build](https://github.com/Turg-er/bajerino/actions/workflows/build.yml/badge.svg?branch=master)](https://github.com/Turg-er/bajerino/actions/workflows/build.yml)

Bajerino is a Twitch and Kick chat client based on [Chatterino 2](https://github.com/Chatterino/chatterino2), [Chatterino7](https://github.com/SevenTV/chatterino7), and selected features from [Moltorino](https://codeberg.org/MoltoBenne/Moltorino).

## Features

### Bajerino additions

- **Per-channel message encryption:** encrypt outgoing messages with a shared password, decrypt compatible incoming messages, and show a lock badge on decrypted chat and pinned messages. Encryption can be controlled from the split input, a hotkey, or `/e` and `/d`.
- **Per-channel anonymous Twitch access:** override the Twitch read identity for individual channels while retaining the signed-in account for whispers, sending, and supported API actions. Anonymous state is persisted and shown in the channel UI.
- **Selective Twitch proxying:** route all traffic, all Twitch traffic, or only authenticated Twitch traffic through `CHATTERINO2_PROXY_URL`. See [Proxying](#proxying) for the exact environment variables and privacy implications.
- **Channel Points automation:** automatically claim bonus chests and optionally simulate watching open, live channels to earn points, with a configurable concurrency limit, priority order, and blacklist.
- **Shared Chatterino settings mode:** optionally use the existing Chatterino settings directory instead of a separate Bajerino directory.
- **Bajerino identity and extras:** Bajerino application names, icons, packaging, the custom Tomas badge, and the optional Big 3 username marker.

### Moltorino features

Bajerino retains selected features from [Moltorino's source repository](https://codeberg.org/MoltoBenne/Moltorino) and the [Moltorino website](https://moltorino.com/). These features have been adapted to Bajerino's current Chatterino7 base.

- **Additional Twitch authentication:** save separate broadcaster or moderator accounts for supported channel-management actions without changing the account used for normal chat.
- **Pinned-message tools:** display Twitch pins, expose moderator pin actions and duration choices, show unpin notifications, and keep encrypted pinned messages readable when the password matches.
- **Polls and predictions:** banners and dialogs for voting, betting, creating, managing, resolving, and dismissing polls and predictions.
- **Channel Points and rewards:** show a points balance beside the input, browse and redeem custom rewards, and control popup behavior after a redemption.
- **Moderation tools:** repeated-message detection, inline moderation and self-delete actions, raid status, chat warnings, and `/nuke`, `/spam`, and `/pyramid` workflows.
- **Translation and client detection:** translate incoming messages from the message menu, preview or send translated outgoing messages, and optionally highlight messages detected as Twitch Web, Android, or iOS clients.
- **Expanded usercards:** optional follower, account, stream, followage, subscription, and chatter details; older-message loading; name history; 7TV profile access; and broadcaster role-management actions.
- **Interface and badge extras:** Moltorino and Homies badges, follow controls in split headers, message-colored tab alerts, and optional hide-to-tray notifications on supported desktops.

### Chatterino7 features

Bajerino tracks [SevenTV/Chatterino7](https://github.com/SevenTV/chatterino7), which extends Chatterino with 7TV-centric and multi-platform functionality.

- **7TV name paints:** gradient and animated image paints on usernames, mentions, replies, and whispers, with configurable shadows.
- **7TV personal emotes:** personal and entitled emote sets in Twitch and Kick messages, completion, and the emote picker. Keep 7TV live updates enabled for entitlement and cosmetic updates.
- **7TV cosmetics:** live paint and badge entitlement updates, optional animated 7TV badges, animated 7TV profile avatars, and shortcuts to 7TV user profiles.
- **Higher-quality images:** 4x image links for 7TV and FFZ plus AVIF support for 7TV images when a decoder is available.
- **Experimental Kick support:** Kick accounts, chat, replies, emotes, badges, history, stream state, highlights, usercards, room modes, and moderation actions.
- **Multi-channel splits:** combine Twitch and Kick channels in one split while selecting which channel supplies the sending and moderation context.
- **Flexible Twitch channel modes:** choose authenticated, anonymous-read, or Bajerino-anonymous behavior per channel, with optional parallel anonymous IRC connections.

## Screenshots

![Example of Personal Emotes](https://user-images.githubusercontent.com/27637025/227032811-837c56eb-7724-431b-b00e-b944c9289dff.png)
![Example of Paints](https://user-images.githubusercontent.com/27637025/227034147-cb1fcd76-dbae-4878-9551-96ffa64dd1a9.png)

## Downloads

Stable Bajerino builds can be downloaded from the [latest release](https://github.com/Turg-er/bajerino/releases/latest).

To test new features, download the [nightly Bajerino build](https://github.com/Turg-er/bajerino/releases/tag/nightly-build).

The macOS build targets macOS 13 or newer on Apple Silicon (ARM64).

Official Chatterino7 builds remain available from the [Chatterino7 releases](https://github.com/SevenTV/chatterino7/releases/latest).

## Issues

Report Bajerino-specific problems in the [Bajerino issue tracker](https://gitlab.com/Turger/bajtv-chatterino2/-/issues). Use the [Chatterino7](https://github.com/SevenTV/chatterino7/issues) or [Chatterino 2](https://github.com/Chatterino/chatterino2/issues) issue tracker only when the problem can also be reproduced in that unmodified upstream project.

## Community

If you don't have a GitHub account and want to report issues or want to join the community you can join the official 7TV Discord using the link here: <https://discord.com/invite/7tv>.

## AVIF Support

When building Chatterino 7, you might not have access to a static build of `libavif`. In that case, you can define `CHATTERINO_NO_AVIF_PLUGIN` in CMake. If you have `qavif.so` from [kimageformats](https://invent.kde.org/frameworks/kimageformats) installed on your system, Chatterino will pick it up and use AVIF images.

## Proxying

If you set `CHATTERINO2_PROXY_URL`, Bajerino proxies all network traffic by default — Qt traffic via the Qt application proxy, and the asio-based connections (EventSub, PubSub, 7TV/BTTV/Kick live updates) explicitly.

Two environment variables narrow that scope:

- `BAJERINO_PROXY_TWITCH=1` proxies only Twitch connections — Helix, Twitch GraphQL, PubSub, EventSub, and IRC. Twitch images and third-party services (7TV, BTTV, Kick) stay direct.
- `BAJERINO_PROXY_TWITCH_AUTHED_ONLY=1` is narrower still: it proxies only authenticated Twitch connections, including Helix, Twitch GraphQL, PubSub, EventSub, and authenticated IRC. Anonymous IRC and third-party services stay direct. (If both are set, this one wins.)

`BAJERINO_PROXY_TWITCH_AUTHED_ONLY` can be combined with `Bajerino anonymous` mode to proxy account traffic while anonymous IRC stays direct. The authenticated write connection is proxied in this mode and does not send `JOIN`, but it still identifies your account to Twitch IRC.

## Original Chatterino 2 Readme

Chatterino 2 is a chat client for [Twitch.tv](https://twitch.tv).
The Chatterino 2 wiki can be found [here](https://wiki.chatterino.com).
Contribution guidelines can be found [here](https://wiki.chatterino.com/Contributing%20for%20Developers).

You might also need to install the [VC++ Redistributables](https://aka.ms/vs/17/release/vc_redist.x64.exe) from Microsoft if you do not have it installed already.
If you still receive an error about `MSVCR120.dll missing`, then you should install the [VC++ 2013 Restributable](https://download.microsoft.com/download/2/E/6/2E61CFA4-993B-4DD4-91DA-3737CD5CD6E3/vcredist_x64.exe).

## Building

To get the Bajerino source code with its required submodules, run:

```shell
git clone --recurse-submodules https://github.com/Turg-er/bajerino.git
```

or

```shell
git clone https://github.com/Turg-er/bajerino.git
cd bajerino
git submodule update --init --recursive
```

- [Building on Windows](BUILDING_ON_WINDOWS.md)
- [Building on Windows with vcpkg](BUILDING_ON_WINDOWS_WITH_VCPKG.md)
- [Building on Linux](BUILDING_ON_LINUX.md)
- [Building on macOS](BUILDING_ON_MAC.md)
- [Building on FreeBSD](BUILDING_ON_FREEBSD.md)

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
