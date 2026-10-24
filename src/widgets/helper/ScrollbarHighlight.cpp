// SPDX-FileCopyrightText: 2017 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "widgets/helper/ScrollbarHighlight.hpp"

#include "Application.hpp"
#include "singletons/Theme.hpp"
#include "widgets/Scrollbar.hpp"

#include <cassert>

namespace chatterino {

ScrollbarHighlight::ScrollbarHighlight()
    : style_(Style::None)
{
}

ScrollbarHighlight::ScrollbarHighlight(const std::shared_ptr<QColor> &color,
                                       Style style)
    : color_(color)
    , style_(style)
{
}

QColor ScrollbarHighlight::getColor() const
{
    assert(this->color_);
    return *this->color_;
}

ScrollbarHighlight::Style ScrollbarHighlight::getStyle() const
{
    return this->style_;
}

bool ScrollbarHighlight::isNull() const
{
    return this->style_ == Style::None || !this->color_ ||
           !this->color_->isValid();
}

}  // namespace chatterino
