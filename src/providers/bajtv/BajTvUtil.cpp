
#include "providers/bajtv/BajTvUtil.hpp"

namespace chatterino {

Url parseBajTvUrl(const QString &bajtvUrl)
{
    QUrl asURL(bajtvUrl);
    asURL.setScheme("http");
    return {asURL.toString()};
}

}  // namespace chatterino
