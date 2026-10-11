# Linux

Use Qt 6, including the Core, Widgets, Network, SVG, SVG Widgets, SQL, and Concurrent modules.

## Install dependencies

### Ubuntu

Building on Ubuntu requires Docker.

Use <https://github.com/Chatterino/docker/pkgs/container/chatterino2-build-ubuntu-22.04> if you're on Ubuntu 22.04.

Use <https://github.com/Chatterino/docker/pkgs/container/chatterino2-build-ubuntu-24.04> if you're on Ubuntu 24.04.

The built binary should be exportable from the final image & able to run on your system assuming you perform a static build. See our [build.yml GitHub workflow file](.github/workflows/build.yml) for the CMake line used for Ubuntu builds.

### Debian 13 (trixie) or later

```sh
sudo apt install qt6-base-dev qt6-svg-dev qt6-image-formats-plugins libboost-dev libnotify-dev libssl-dev pkg-config cmake g++ git hunspell libwebp-dev libvpx-dev libyuv-dev libsecret-1-dev libwebkit2gtk-4.1-dev libjson-glib-dev ffmpeg
```

### Arch Linux

```sh
sudo pacman -S --needed qt6-base qt6-tools boost-libs openssl qt6-imageformats qt6-svg boost libnotify rapidjson pkgconf cmake hunspell libwebp libvpx libyuv libsecret webkit2gtk-4.1 json-glib ffmpeg
```

If you use Wayland, you will also need to ensure `qt6-wayland` is installed.

Alternatively you can use the [chatterino2-git](https://aur.archlinux.org/packages/chatterino2-git/) package to build and install Chatterino for you.

### openSUSE

```sh
sudo zypper install cmake pkgconf boost-devel libboost_json1_89_0-devel desktop-file-utils libappstream-glib8 hunspell ninja doxygen qt6-tools-devel
```

### Gentoo Linux

```sh
doas emerge dev-libs/openssl dev-qt/qt5compat dev-qt/qtbase dev-qt/qtsvg dev-qt/qtimageformats x11-libs/libnotify dev-libs/boost dev-build/cmake app-text/hunspell
```

### Fedora 42 and above

_Most likely works the same for other Red Hat-like distros. Substitute `dnf` with `yum`._

```sh
sudo dnf install qt6-qtbase-devel qt6-qtimageformats qt6-qtsvg-devel qt6-qtbase-private-devel g++ git openssl-devel libnotify-devel boost-devel cmake hunspell-devel
```

### NixOS 18.09+

```sh
nix-shell -p openssl boost qt6.full pkg-config cmake libnotify hunspell
```

## Media and account features

All builds need WebP (including `webpdemux`), libvpx, libyuv, and the incremental
libwebm parser. CMake downloads the pinned libwebm parser when the installed
package does not provide it. YouTube and TikTok accounts use the system secret
service through the keychain library; install libsecret development files.
TikTok login additionally needs WebKitGTK 4.1 version 2.42 or newer and JSON-GLib.
The browser runs in a separate helper installed alongside Bajerino.

Background video import uses `ffmpeg` and `ffprobe` from `PATH` for local builds.
Release packages use the pinned, limited codec build produced by
[.CI/build-video-dependencies.sh](.CI/build-video-dependencies.sh). Set
`BAJERINO_MEDIA_TOOLS_DIR` to that bundle's `support/media` directory to include
its executables and licenses in a package. Add the dependency prefix to
`CMAKE_PREFIX_PATH` when using the script's static codec libraries.

YouTube reading works without OAuth credentials. To enable Google sign-in in
an independently distributed build, set `MOLTORINO_YOUTUBE_OAUTH_CLIENT_ID` and
`MOLTORINO_YOUTUBE_OAUTH_CLIENT_SECRET` for a desktop OAuth client. The imported
CMake option names are retained for compatibility with Moltorino's provider code.
Account support can be disabled explicitly with `MOLTORINO_YOUTUBE_ACCOUNTS=OFF`
or `MOLTORINO_TIKTOK_ACCOUNTS=OFF` when building without their dependencies.

## Compile

### Windows + WSL (Ubuntu 22.04)

Bajerino ships helper scripts for Linux packaging from Windows through WSL:

```bat
build_linux_appimage.bat
build_linux_flatpak.bat
```

Those wrappers call the WSL scripts in `.CI/` and place finished artifacts in `releases/linux/`.

Prerequisites for the WSL path:

```sh
sudo apt install ninja-build build-essential pkg-config libgl1-mesa-dev libxkbcommon-dev libdbus-1-dev libxcb-cursor-dev libx11-xcb-dev libxkbcommon-x11-dev libwayland-dev libsecret-1-dev libpulse-dev libasound2-dev libfontconfig1-dev libfuse2 desktop-file-utils patchelf flatpak flatpak-builder rsync python3-pip
python3 -m pip install --user aqtinstall
~/.local/bin/aqt install-qt linux desktop 6.10.2 linux_gcc_64 -O /home/$USER/Qt --modules qtimageformats qt5compat
flatpak remote-add --if-not-exists --user flathub https://dl.flathub.org/repo/flathub.flatpakrepo
flatpak install --user flathub org.kde.Platform//6.10 org.kde.Sdk//6.10
```

The WSL scripts assume Qt is installed at `/home/<user>/Qt/6.10.2/gcc_64`. Set `QT_ROOT_DIR` before running the AppImage script if your Qt path differs.

## Manually

1. In the project directory, create a build directory and enter it
   ```sh
   mkdir build
   cd build
   ```
1. Generate build files. To enable Lua plugins in your build add `-DCHATTERINO_PLUGINS=ON` to this command.
   ```sh
   cmake -DCHATTERINO_SPELLCHECK=On ..
   ```
1. Build the project
   ```sh
   cmake --build .
   ```

### Through Qt Creator

1. Install C++ IDE Qt Creator by using `sudo apt install qtcreator` (Or whatever equivalent for your distro)
1. Open `CMakeLists.txt` with Qt Creator and select build
