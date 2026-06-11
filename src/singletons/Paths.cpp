// SPDX-FileCopyrightText: 2018 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "singletons/Paths.hpp"

#include "common/Modes.hpp"
#include "singletons/Settings.hpp"
#include "util/CombinePath.hpp"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

#include <cassert>
#include <vector>

using namespace std::literals;

namespace chatterino {

namespace {

bool copyRecursively(const QString &sourcePath, const QString &destinationPath)
{
    const QDir sourceDir(sourcePath);
    if (!sourceDir.exists())
    {
        return false;
    }

    if (!QDir().mkpath(destinationPath))
    {
        return false;
    }

    QDirIterator it(sourcePath, QDir::NoDotAndDotDot | QDir::AllEntries,
                    QDirIterator::Subdirectories);
    while (it.hasNext())
    {
        const auto sourceEntryPath = it.next();
        const QFileInfo sourceInfo(sourceEntryPath);
        const auto relativePath = sourceDir.relativeFilePath(sourceEntryPath);
        const auto destinationEntryPath =
            combinePath(destinationPath, relativePath);

        if (sourceInfo.isDir())
        {
            if (!QDir().mkpath(destinationEntryPath))
            {
                return false;
            }
            continue;
        }

        const auto destinationDir = QFileInfo(destinationEntryPath).dir();
        if (!destinationDir.exists() && !QDir().mkpath(destinationDir.path()))
        {
            return false;
        }

        if (QFile::exists(destinationEntryPath) &&
            !QFile::remove(destinationEntryPath))
        {
            return false;
        }

        if (!QFile::copy(sourceEntryPath, destinationEntryPath))
        {
            return false;
        }
    }

    return true;
}

void tryMigrateLinuxSettingsInto(const QString &destinationPath)
{
    const auto destinationSettings =
        combinePath(destinationPath, "Settings/settings.json");
    if (QFileInfo::exists(destinationSettings))
    {
        return;
    }

    std::vector<QString> candidatePaths;

    if (qEnvironmentVariableIsSet("FLATPAK_ID"))
    {
        candidatePaths.emplace_back(
            QDir::homePath() +
            "/.var/app/com.chatterino.chatterino/data/chatterino");
    }

    candidatePaths.emplace_back(QDir::homePath() + "/.local/share/chatterino");

    for (const auto &candidatePath : candidatePaths)
    {
        const auto candidateSettings =
            combinePath(candidatePath, "Settings/settings.json");
        if (!QFileInfo::exists(candidateSettings))
        {
            continue;
        }

        copyRecursively(candidatePath, destinationPath);
        return;
    }
}

}  // namespace

Paths::Paths()
{
    this->initAppFilePathHash();

    this->initCheckPortable();
    this->initRootDirectory();
    this->initSubDirectories();
}

bool Paths::createFolder(const QString &folderPath)
{
    return QDir().mkpath(folderPath);
}

bool Paths::isPortable() const
{
    return Modes::instance().isPortable;
}

QString Paths::cacheDirectory() const
{
    static const auto pathSetting = [] {
        QStringSetting cachePathSetting("/cache/path");

        cachePathSetting.connect([](const auto &newPath) {
            if (!newPath.isEmpty())
            {
                QDir().mkpath(newPath);
            }
        });

        return cachePathSetting;
    }();

    auto path = pathSetting.getValue();

    if (path.isEmpty())
    {
        return this->cacheDirectory_;
    }

    return path;
}

QString Paths::cacheFilePath(const QString &fileName) const
{
    return combinePath(this->cacheDirectory(), fileName);
}

void Paths::initAppFilePathHash()
{
    this->applicationFilePathHash =
        QCryptographicHash::hash(
            QCoreApplication::applicationFilePath().toUtf8(),
            QCryptographicHash::Sha224)
            .toBase64()
            .mid(0, 32)
            .replace("+", "-")
            .replace("/", "x");
}

void Paths::initCheckPortable()
{
    this->portable_ = QFileInfo::exists(
        combinePath(QCoreApplication::applicationDirPath(), "portable"));
}

void Paths::initRootDirectory()
{
    assert(this->portable_.has_value());

    // Root path = %APPDATA%/Chatterino or the folder that the executable
    // resides in

    this->rootAppDataDirectory = [&]() -> QString {
        // portable
        if (Modes::instance().isPortable)
        {
            return QCoreApplication::applicationDirPath();
        }

        // permanent installation
        QString path =
            QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        if (path.isEmpty())
        {
            throw std::runtime_error("Could not create directory \""s +
                                     path.toStdString() + "\"");
        }

        if (Modes::instance().useChatterinoDirectory)
        {
#ifdef Q_OS_WIN
            path.replace("bajerino", "Chatterino");
#else
            path.replace("bajerino", "chatterino");
#endif
        }

// create directory Chatterino2 instead of Chatterino on windows because the
// ladder one is takes by Chatterino 1 already
#ifdef Q_OS_WIN
        path.replace("bajerino", "Bajerino");

        path += "2";
#endif
        return path;
    }();
}

void Paths::initSubDirectories()
{
    // required the app data directory to be set first
    assert(!this->rootAppDataDirectory.isEmpty());

    // create settings subdirectories and validate that they are created
    // properly
    auto makePath = [&](const QString &name) -> QString {
        auto path = combinePath(this->rootAppDataDirectory, name);

        if (!QDir().mkpath(path))
        {
            throw std::runtime_error("Could not create directory \""s +
                                     path.toStdString() + "\"");
        }

        return path;
    };

    makePath("");
    this->settingsDirectory = makePath("Settings");
    this->cacheDirectory_ = makePath("Cache");
    this->messageLogDirectory = makePath("Logs");
    this->miscDirectory = makePath("Misc");
    this->twitchProfileAvatars =
        makePath(combinePath("ProfileAvatars", "twitch"));
    this->pluginsDirectory = makePath("Plugins");
    this->themesDirectory = makePath("Themes");
    this->crashdumpDirectory = makePath("Crashes");
    this->dictionariesDirectory = makePath("Dictionaries");
#ifdef Q_OS_WIN
    this->ipcDirectory = makePath("IPC");
#else
    // NOTE: We do *NOT* use IPC on non-Windows platforms.
    // If we start, we should re-consider this directory.
    this->ipcDirectory = "/tmp";
#endif
}

}  // namespace chatterino
