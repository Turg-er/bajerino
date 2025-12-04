#include "util/BajerinoHelpers.hpp"

#include "common/Literals.hpp"  // IWYU pragma: keep
#include "singletons/Settings.hpp"

namespace chatterino {

using namespace chatterino::literals;

bool isBig3(QStringView userID)
{
    return getSettings()->big3Noticer &&
           (userID == u"1366614319"_s ||  // justonemoreaccountbro
            userID == u"1351960573"_s ||  // seedingseedman
            userID == u"1394928763"_s);   // fango_ohio67
}

QString noticeBig3(const QString &username)
{
    return u"((("_s + username + u")))"_s;
}
}  // namespace chatterino
