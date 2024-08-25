
#include "providers/bajtv/BajTvUtil.hpp"

namespace chatterino {

Url parseBajTvUrl(const QString &bajtvUrl)
{
    QUrl asURL(bajtvUrl);
    asURL.setScheme("https");
    return {asURL.toString()};
}

}  // namespace chatterino
