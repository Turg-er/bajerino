#!/usr/bin/env bash

# Bundle relevant qt & system dependencies into the ./bajerino.app folder

set -eo pipefail

if [ -d bin/bajerino.app ] && [ ! -d bajerino.app ]; then
    >&2 echo "Moving bin/bajerino.app down one directory"
    mv bin/bajerino.app bajerino.app
fi

if [ -n "$Qt6_DIR" ]; then
    echo "Using Qt DIR from Qt6_DIR: $Qt6_DIR"
    _QT_DIR="$Qt6_DIR"
fi

if [ -n "$_QT_DIR" ]; then
    export PATH="${_QT_DIR}/bin:$PATH"
else
    echo "No Qt environment variable set, assuming system-installed Qt"
fi

echo "Running MACDEPLOYQT"

_macdeployqt_args=(-no-plugins -no-codesign)

echo "Extracting kimageformats plugins"
7z e -okimg kimg.zip

mkdir -p bajerino.app/Contents/Frameworks
mkdir -p bajerino.app/Contents/PlugIns/imageformats
cp kimg/libKF6Archive.6.dylib bajerino.app/Contents/Frameworks/
cp kimg/kimg_avif.dylib bajerino.app/Contents/PlugIns/imageformats/

# Stage the plugins used by the application before macdeployqt resolves their
# dependencies. Its default QtSql deployment includes database drivers requiring
# external Mimer, ODBC, and PostgreSQL installations; recordings only use SQLite.
qt_plugins_dir=$(qtpaths --query QT_INSTALL_PLUGINS)
bundle_plugins_dir=bajerino.app/Contents/PlugIns
for plugin in platforms/libqcocoa.dylib sqldrivers/libqsqlite.dylib; do
    mkdir -p "$bundle_plugins_dir/$(dirname "$plugin")"
    cp "$qt_plugins_dir/$plugin" "$bundle_plugins_dir/$plugin"
done

for plugin_type in printsupport styles tls networkinformation imageformats iconengines platforminputcontexts; do
    for plugin in "$qt_plugins_dir/$plugin_type/"*.dylib; do
        [ -f "$plugin" ] || continue
        case "$plugin" in
            *_debug.dylib) continue ;;
        esac
        mkdir -p "$bundle_plugins_dir/$plugin_type"
        cp "$plugin" "$bundle_plugins_dir/$plugin_type/"
    done
done

# Include the converter binaries when resolving and rewriting library paths.
for program in ffmpeg ffprobe; do
    executable="bajerino.app/Contents/MacOS/$program"
    if [ -f "$executable" ]; then
        _macdeployqt_args+=("-executable=$executable")
    fi
done

macdeployqt bajerino.app "${_macdeployqt_args[@]}" -verbose=1

# Deployment rewrites Mach-O load commands, invalidating existing signatures.
# Sign every bundled binary afterwards, including libraries such as KF6Archive
# that macdeployqt's dependency-based signing can miss. Seal the app last.
codesign_identity=${MACOS_CODESIGN_CERTIFICATE:--}
while IFS= read -r -d '' binary; do
    # Signing the main executable also seals its containing app bundle.
    # Leave it for the final app signature, after all nested code is signed.
    [ "$binary" = bajerino.app/Contents/MacOS/bajerino ] && continue
    case "$(file -b "$binary")" in
        *Mach-O*) codesign --force --sign "$codesign_identity" "$binary" ;;
    esac
done < <(find bajerino.app/Contents -type f -print0)

# Seal framework resources and any nested bundles after their binaries.
while IFS= read -r -d '' bundle; do
    codesign --force --sign "$codesign_identity" "$bundle"
done < <(find bajerino.app/Contents -depth -type d \( -name '*.framework' -o -name '*.app' -o -name '*.xpc' \) -print0)

codesign --force --sign "$codesign_identity" bajerino.app
codesign --verify --deep --strict bajerino.app

for program in ffmpeg ffprobe; do
    executable="bajerino.app/Contents/MacOS/$program"
    if [ -f "$executable" ]; then
        "$executable" -version > /dev/null
    fi
done
