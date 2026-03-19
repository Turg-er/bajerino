// SPDX-FileCopyrightText: 2017 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "common/network/NetworkManager.hpp"

#include "common/Env.hpp"
#include "providers/NetworkConfigurationProvider.hpp"

#include <QNetworkAccessManager>
#include <QNetworkProxy>

namespace chatterino {

QThread *NetworkManager::workerThread = nullptr;
QNetworkAccessManager *NetworkManager::accessManager = nullptr;
QNetworkAccessManager *NetworkManager::proxiedAccessManager = nullptr;

void NetworkManager::init()
{
    assert(!NetworkManager::workerThread);
    assert(!NetworkManager::accessManager);
    assert(!NetworkManager::proxiedAccessManager);

    const auto &env = Env::get();

    NetworkManager::workerThread = new QThread;
    NetworkManager::workerThread->setObjectName("NetworkWorker");
    NetworkManager::workerThread->start();

    NetworkManager::accessManager = new QNetworkAccessManager;
    if (env.proxyTwitchApiOnly)
    {
        NetworkManager::accessManager->setProxy(
            QNetworkProxy(QNetworkProxy::NoProxy));
    }
    NetworkManager::accessManager->moveToThread(NetworkManager::workerThread);

    if (env.proxyTwitchApiOnly)
    {
        if (const auto proxy = NetworkConfigurationProvider::proxyFromEnv(env))
        {
            NetworkManager::proxiedAccessManager = new QNetworkAccessManager;
            NetworkManager::proxiedAccessManager->setProxy(*proxy);
            NetworkManager::proxiedAccessManager->moveToThread(
                NetworkManager::workerThread);
        }
    }
}

void NetworkManager::deinit()
{
    assert(NetworkManager::workerThread);
    assert(NetworkManager::accessManager);

    // delete the access manager first:
    // - put the event on the worker thread
    // - wait for it to process
    NetworkManager::accessManager->deleteLater();
    NetworkManager::accessManager = nullptr;

    if (NetworkManager::proxiedAccessManager)
    {
        NetworkManager::proxiedAccessManager->deleteLater();
        NetworkManager::proxiedAccessManager = nullptr;
    }

    if (NetworkManager::workerThread)
    {
        NetworkManager::workerThread->quit();
        NetworkManager::workerThread->wait();
    }

    NetworkManager::workerThread->deleteLater();
    NetworkManager::workerThread = nullptr;
}

}  // namespace chatterino
