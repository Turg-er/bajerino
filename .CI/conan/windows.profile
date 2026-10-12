include(default)

[buildenv]
# libvpx invokes MSBuild from Conan's MSYS environment. Preserve its tool paths
# so the generated Visual Studio projects can find Conan's yasm executable.
libvpx/*:UseEnv=True

# FFmpeg runs configure from its build folder, with Conan's .pc files in conan/.
# A relative native search path avoids mixed Windows/MSYS PKG_CONFIG_PATH lists.
ffmpeg/*:PKG_CONFIG_LIBDIR=conan
