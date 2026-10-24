// SPDX-FileCopyrightText: 2024 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "MessageBuilding.hpp"
#include "providers/recentmessages/Impl.hpp"

#include <benchmark/benchmark.h>
#include <QJsonObject>

using namespace chatterino;

namespace {

class ValidateRecentMessages : public bench::MessageBenchmark
{
public:
    using MessageBenchmark::MessageBenchmark;

    void run(benchmark::State &state) override
    {
        const auto messages =
            this->messages.object().value("messages").toArray();
        for (auto _ : state)
        {
            const auto valid =
                recentmessages::detail::hasParseableRecentMessages(messages);
            benchmark::DoNotOptimize(valid);
        }
    }
};

class BuildRecentMessages : public bench::MessageBenchmark
{
public:
    using MessageBenchmark::MessageBenchmark;

    void run(benchmark::State &state) override
    {
        const auto messages =
            this->messages.object().value("messages").toArray();
        for (auto _ : state)
        {
            const auto built = bench::buildRecentMessages(messages, this->chan);
            benchmark::DoNotOptimize(built);
        }
    }
};

void BM_ValidateRecentMessages(benchmark::State &state, const QString &name)
{
    ValidateRecentMessages bench(name);
    bench.run(state);
}

void BM_BuildRecentMessages(benchmark::State &state, const QString &name)
{
    BuildRecentMessages bench(name);
    bench.run(state);
}

}  // namespace

BENCHMARK_CAPTURE(BM_ValidateRecentMessages, nymn, "nymn");
BENCHMARK_CAPTURE(BM_BuildRecentMessages, nymn, "nymn");
