// SPDX-FileCopyrightText: 2024 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include <version>

// NOTE: require 202211L (not 202202L): GCC 12 defines std::expected at
// 202202L but without the monadic operations (and_then/transform/or_else),
// which only arrived in GCC 13 (202211L). Gating lower would select a
// std::expected that's missing and_then and break callers on GCC 12.
#if __cpp_lib_expected >= 202211L
#    include <expected>
#else
#    define CHATTERINO_USING_NONSTD_EXPECTED
#    include <nonstd/expected.hpp>
#endif

#include <type_traits>

class QString;

namespace chatterino {

#if __cpp_lib_expected >= 202211L
template <typename T, typename E>
using Expected = std::expected<T, E>;

template <typename E>
constexpr std::unexpected<std::decay_t<E>> makeUnexpected(E &&value)
{
    return std::unexpected<std::decay_t<E>>(std::forward<E>(value));
}
#else
template <typename T, typename E>
using Expected = nonstd::expected_lite::expected<T, E>;

template <typename E>
constexpr nonstd::unexpected<std::decay_t<E>> makeUnexpected(E &&value)
{
    return nonstd::unexpected<std::decay_t<E>>(std::forward<E>(value));
}
#endif

template <typename T>
using ExpectedStr = Expected<T, QString>;

}  // namespace chatterino
