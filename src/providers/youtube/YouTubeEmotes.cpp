#include "providers/youtube/YouTubeEmotes.hpp"

#include "messages/Image.hpp"
#include "providers/youtube/YouTubeTypes.hpp"
#include "util/QStringHash.hpp"

#include <QByteArrayView>
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QSet>
#include <QStringBuilder>
#include <QStringList>
#include <QStringView>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

using namespace chatterino;
using namespace Qt::Literals::StringLiterals;

constexpr qsizetype MAX_HTML_BYTES = 4 * 1024 * 1024;
constexpr std::size_t MAX_CACHE_ENTRIES = 24;
constexpr std::size_t MAX_IN_FLIGHT = 4;
constexpr std::size_t MAX_JSON_NODES = 100'000;
constexpr std::size_t MAX_EMOTES = 512;
constexpr std::size_t MAX_SHORTCUTS = 2'048;
constexpr int MAX_JSON_DEPTH = 64;
constexpr int REQUEST_TIMEOUT_MS = 8'000;
constexpr auto FAILURE_CACHE_TTL = std::chrono::minutes(2);

constexpr auto YOUTUBE_USER_AGENT =
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
    "AppleWebKit/537.36 (KHTML, like Gecko) Chrome/138.0.0.0 "
    "Safari/537.36 Moltorino/7.5";

struct GlobalEmote {
    QStringView shortcut;
    QStringView imageUrl;
};

constexpr auto GLOBAL_EMOTES = std::to_array<GlobalEmote>({
    {u":hand-pink-waving:",
     u"https://yt3.ggpht.com/"
     u"KOxdr_z3A5h1Gb7kqnxqOCnbZrBmxI2B_tRQ453BhTWUhYAlpg5ZP8IKEBkcvRoY8grY91Q="
     u"w24-h24-c-k-nd"},
    {u":face-blue-smiling:",
     u"https://yt3.ggpht.com/"
     u"cktIaPxFwnrPwn-alHvnvedHLUJwbHi8HCK3AgbHpphrMAW99qw0bDfxuZagSY5ieE9BBrA="
     u"w24-h24-c-k-nd"},
    {u":face-red-droopy-eyes:",
     u"https://yt3.ggpht.com/"
     u"oih9s26MOYPWC_uL6tgaeOlXSGBv8MMoDrWzBt-80nEiVSL9nClgnuzUAKqkU9_TWygF6CI="
     u"w24-h24-c-k-nd"},
    {u":face-purple-crying:",
     u"https://yt3.ggpht.com/"
     u"g6_km98AfdHbN43gvEuNdZ2I07MmzVpArLwEvNBwwPqpZYzszqhRzU_DXALl11TchX5_xFE="
     u"w24-h24-c-k-nd"},
    {u":text-green-game-over:", u"https://yt3.ggpht.com/"
                                u"cr36FHhSiMAJUSpO9XzjbOgxhtrdJNTVJUlMJeOOfLOFz"
                                u"KleAKT2SEkZwbqihBqfTXYCIg=w24-h24-c-k-nd"},
    {u":text-green-gg:",
     u"data:image/"
     u"png;base64,"
     u"iVBORw0KGgoAAAANSUhEUgAAABgAAAAYCAYAAADgdz34AAAAAXNSR0IArs4c6QAABVNJREFU"
     u"SEtllglQFkQUx3/"
     u"LrSJ5wGBpVApqniMoSmCBOQKOYCKapoxaggfmhWBAQiiYYFBpgiVeoYPkAZqFeEEjCghqKiN"
     u"WitNERSBhXigg2+y34Ae6M8w37Pf2/"
     u"f7v7XtvP0H7FSBHIYjmCWMww44m6mmhhBpiKBEXOtiCGdNlBA+"
     u"ZgAn9MKETkmq6ksseEQU0KXvx9NAsGQ8ymoAceC0aulbAY+DPVMgOgmrrFHJEmMF+"
     u"vvSkXubjfhlGZkK3JDAF/"
     u"g2HY0uhsidkdu4B1GvAdOmN9eNjRFpBV+AJIFvRJkALsKUGrtstIlvsJlA+"
     u"JPgTGBGndba3VaD0m3C9bykZwlUD/"
     u"GQtUYtteSVNO392KcgdIFpKLEhnbGkw77rqCJ9dyuN9IEbCd8JK/"
     u"etEWNWvrO5jPKBUqNUeZgnE/wE1fWClF/"
     u"Qu0JEpW+WluR3JAki5AYn93hR4y2WE5H2Bm492aAZc2gqNXWB0kPGgObD3HBS6QarQ+"
     u"8r5lRQoHwvTRoGKVC3l4+"
     u"gZ2OkRJPCVu4j4eA79E3Qu64AoqdUpR8qx2leHsgvhsiOs7QWNgFIaVQN37GDRWhgUq0Uq8N"
     u"l82O25XOAjC4id8RYOWTrUK1/"
     u"DnhB9ebECbFoByllGMdR2hxUD9PcKvkSCFTBrHwyfaQScPw3pXmGCifIq8S5DsL+"
     u"oAcUnIedtHapfOngF6wiqnGFjGQy8DCEjdIoUJEJCZ8A3D8b56H0FziqChDEBgknydxKFAy+"
     u"0hnbiFJwap8N8oFLTANIEmizBGnC8AB+M1I7aqkWdbWqEiQVgUw9/"
     u"2UOeezNZFuYCP1lHsuhhUKGc5uZDric4n4ceDfDIHBos4L4lVA6F4aUwx1UD6oG1Ukeo/"
     u"pQg9akqzkq2YCNmC96RD0kWnQybCnC4CBxPwrA1xjJVqVMVciMITq+C+"
     u"cM1oM4WtvwCsT2NxaD2VQFUB0DywUbBVNlMkjB9CjhaDEM2gEOOrqS2pQA1A+HgIVgySMP/"
     u"9oCKRBjv3rEPlCAlOLIOQaBsIUkIQ8mpCI6fA8fN0Dez4yEDYBAcPNAO4A7XkmCCe+"
     u"toaydIAVbdfyCYIhtIFFaGO1BOSvKhsjd4zIMXzxrHoeqDK3FQMB2Wva4jqHGC7UWwyFYXQF"
     u"ujKaH3gHB5W+Avq0kQ9nRv68ytsH2BvrDwpeC0WUN+3gppC2DoNVg6WEf3n+oVCY/"
     u"UhD0BrhP0Jd+aAXGZ8CpRqkzLifEZzMt5UO8GdYMhbZt2Ou9TGKRGO1C+"
     u"GXYtAftbsLqvBqgoVkjooibyKXAZrwGVwbD+m6PkCj81Kg4Tsc4f+"
     u"xgoKgGv0bBY6ktaGQqOqRp2NQ0yFkKn2xBv125U1EKNLXy4EYZEaMBv4ZCYtI9cMVPQS4aTk"
     u"ZZEQ2c4MgmSbKHiI6h5A5z9dV5VTi8dgq+m7MOGGWwSGqDAdwfCzRgY9p7R9tp6+DxyNz+"
     u"IuYJ7lHH0Rxe+99XDy+k6TF4IPX8yVpECFOXDDs9QLOQmEkxMn86oth5pG+"
     u"3K9uJhSPdPJUeECiSnycrxInuyzqVtFUzbCC9tMvaBStfOC7Da2ZupRDI7yxO3Gc+"
     u"XprorJfLbMih2Wc5B8aV+"
     u"0SQ7ObJrLgcCIcVaX15bk6nyrHwfkrfBflPBTOmGkOdYYw7dn3R8lJT6fzwg/"
     u"gzsF2pC3TU++qW0UFEocCiERiswvQuN/"
     u"aHcCYqGQzdTb3aI4wZBQTKMZvkZfifglb1geREah0JVEOT4qvEdyQ6xQZkaAZJQbrIORxmCN"
     u"+4IbDGjAXNKyRbbnn98bQcQWLsOiScWhp84qivKsCKOPeJMm/3/"
     u"v0TZkypOgRoAAAAASUVORK5CYII="},
    {u":person-turqouise-waving:",
     u"https://yt3.ggpht.com/"
     u"uNSzQ2M106OC1L3VGzrOsGNjopboOv-m1bnZKFGuh0DxcceSpYHhYbuyggcgnYyaF3o-AQ="
     u"w24-h24-c-k-nd"},
    {u":face-green-smiling:",
     u"https://yt3.ggpht.com/"
     u"G061SAfXg2bmG1ZXbJsJzQJpN8qEf_W3f5cb5nwzBYIV58IpPf6H90lElDl85iti3HgoL3o="
     u"w24-h24-c-k-nd"},
    {u":face-orange-frowning:",
     u"https://yt3.ggpht.com/"
     u"Ar8jaEIxzfiyYmB7ejDOHba2kUMdR37MHn_R39mtxqO5CD4aYGvjDFL22DW_Cka6LKzhGDk="
     u"w24-h24-c-k-nd"},
    {u":eyes-purple-crying:", u"https://yt3.ggpht.com/"
                              u"FrYgdeZPpvXs-6Mp305ZiimWJ0wV5bcVZctaUy80mnIdwe-"
                              u"P8HRGYAm0OyBtVx8EB9_Dxkc=w24-h24-c-k-nd"},
    {u":face-fuchsia-wide-eyes:",
     u"https://yt3.ggpht.com/"
     u"zdcOC1SMmyXJOAddl9DYeEFN9YYcn5mHemJCdRFQMtDuS0V-IyE-5YjNUL1tduX1zs17tQ="
     u"w24-h24-c-k-nd"},
    {u":cat-orange-whistling:",
     u"https://yt3.ggpht.com/"
     u"0ocqEmuhrKCK87_J21lBkvjW70wRGC32-"
     u"Buwk6TP4352CgcNjL6ug8zcsel6JiPbE58xhq5g=w24-h24-c-k-nd"},
    {u":face-blue-wide-eyes:", u"https://yt3.ggpht.com/"
                               u"2Ht4KImoWDlCddiDQVuzSJwpEb59nZJ576ckfaMh57oqz2"
                               u"pUkkgVTXV8osqUOgFHZdUISJM=w24-h24-c-k-nd"},
    {u":face-orange-raised-eyebrow:",
     u"https://yt3.ggpht.com/"
     u"JbCfmOgYI-mO17LPw8e_ycqbBGESL8AVP6i7ZsBOVLd3PEpgrfEuJ9rEGpP_unDcqgWSCg="
     u"w24-h24-c-k-nd"},
    {u":face-fuchsia-tongue-out:",
     u"https://yt3.ggpht.com/"
     u"EURfJZi_heNulV3mfHzXBk8PIs9XmZ9lOOYi5za6wFMCGrps4i2BJX9j-H2gK6LIhW6h7sY="
     u"w24-h24-c-k-nd"},
    {u":face-orange-biting-nails:",
     u"https://yt3.ggpht.com/"
     u"HmsXEgqUogkQOnL5LP_FdPit9Z909RJxby-uYcPxBLNhaPyqPTcGwvGaGPk2hzB_cC0hs_"
     u"pV=w24-h24-c-k-nd"},
    {u":face-red-heart-shape:",
     u"https://yt3.ggpht.com/"
     u"I0Mem9dU_IZ4a9cQPzR0pUJ8bH-882Eg0sDQjBmPcHA6Oq0uXOZcsjPvPbtormx91Ha2eRA="
     u"w24-h24-c-k-nd"},
    {u":face-fuchsia-poop-shape:",
     u"https://yt3.ggpht.com/"
     u"_xlyzvSimqMzhdhODyqUBLXIGA6F_d5en2bq-AIfc6fc3M7tw2jucuXRIo5igcW3g9VVe3A="
     u"w24-h24-c-k-nd"},
    {u":face-purple-wide-eyes:",
     u"https://yt3.ggpht.com/"
     u"5RDrtjmzRQKuVYE_FKPUHiGh7TNtX5eSNe6XzcSytMsHirXYKunxpyAsVacTFMg0jmUGhQ="
     u"w24-h24-c-k-nd"},
    {u":glasses-purple-yellow-diamond:",
     u"https://yt3.ggpht.com/"
     u"EnDBiuksboKsLkxp_CqMWlTcZtlL77QBkbjz_rLedMSDzrHmy_6k44YWFy2rk4I0LG6K2KI="
     u"w24-h24-c-k-nd"},
    {u":face-pink-tears:",
     u"https://yt3.ggpht.com/"
     u"RL5QHCNcO_Mc98SxFEblXZt9FNoh3bIgsjm0Kj8kmeQJWMeTu7JX_NpICJ6KKwKT0oVHhAA="
     u"w24-h24-c-k-nd"},
    {u":body-blue-raised-arms:",
     u"https://yt3.ggpht.com/"
     u"2Jds3I9UKOfgjid97b_nlDU4X2t5MgjTof8yseCp7M-"
     u"6ZhOhRkPGSPfYwmE9HjCibsfA1Uzo=w24-h24-c-k-nd"},
    {u":hand-orange-covering-eyes:",
     u"https://yt3.ggpht.com/"
     u"y8ppa6GcJoRUdw7GwmjDmTAnSkeIkUptZMVQuFmFaTlF_CVIL7YP7hH7hd0TJbd8p9w67IM="
     u"w24-h24-c-k-nd"},
    {u":trophy-yellow-smiling:",
     u"https://yt3.ggpht.com/"
     u"7tf3A_D48gBg9g2N0Rm6HWs2aqzshHU4CuVubTXVxh1BP7YDBRC6pLBoC-ibvr-zCl_Lgg="
     u"w24-h24-c-k-nd"},
    {u":eyes-pink-heart-shape:", u"https://yt3.ggpht.com/"
                                 u"5vzlCQfQQdzsG7nlQzD8eNjtyLlnATwFwGvrMpC8dgLc"
                                 u"osNhWLXu8NN9qIS3HZjJYd872dM=w24-h24-c-k-nd"},
    {u":face-turquoise-covering-eyes:",
     u"https://yt3.ggpht.com/"
     u"H2HNPRO8f4SjMmPNh5fl10okSETW7dLTZtuE4jh9D6pSmaUiLfoZJ2oiY-qWU3Owfm1IsXg="
     u"w24-h24-c-k-nd"},
    {u":hand-green-crystal-ball:",
     u"https://yt3.ggpht.com/"
     u"qZfJrWDEmR03FIak7PMNRNpMjNsCnOzD9PqK8mOpAp4Kacn_uXRNJNb99tE_"
     u"1uyEbvgJReF2=w24-h24-c-k-nd"},
    {u":face-turquoise-drinking-coffee:",
     u"https://yt3.ggpht.com/"
     u"myqoI1MgFUXQr5fuWTC9mz0BCfgf3F8GSDp06o1G7w6pTz48lwARjdG8vj0vMxADvbwA1dA="
     u"w24-h24-c-k-nd"},
    {u":body-green-covering-eyes:",
     u"https://yt3.ggpht.com/"
     u"UR8ydcU3gz360bzDsprB6d1klFSQyVzgn-Fkgu13dIKPj3iS8OtG1bhBUXPdj9pMwtM00ro="
     u"w24-h24-c-k-nd"},
    {u":goat-turquoise-white-horns:",
     u"https://yt3.ggpht.com/"
     u"jMnX4lu5GnjBRgiPtX5FwFmEyKTlWFrr5voz-Auko35oP0t3-zhPxR3PQMYa-7KhDeDtrv4="
     u"w24-h24-c-k-nd"},
    {u":hand-purple-blue-peace:",
     u"https://yt3.ggpht.com/"
     u"-sC8wj6pThd7FNdslEoJlG4nB9SIbrJG3CRGh7-bNV0RVfcrJuwiWHoUZ6UmcVs7sQjxTg4="
     u"w24-h24-c-k-nd"},
    {u":face-blue-question-mark:",
     u"https://yt3.ggpht.com/"
     u"Wx4PMqTwG3f4gtR7J9Go1s8uozzByGWLSXHzrh3166ixaYRinkH_F05lslfsRUsKRvHXrDk="
     u"w24-h24-c-k-nd"},
    {u":face-blue-covering-eyes:",
     u"https://yt3.ggpht.com/"
     u"kj3IgbbR6u-mifDkBNWVcdOXC-ut-tiFbDpBMGVeW79c2c54n5vI-HNYCOC6XZ9Bzgupc10="
     u"w24-h24-c-k-nd"},
    {u":face-purple-smiling-fangs:",
     u"https://yt3.ggpht.com/"
     u"k1vqi6xoHakGUfa0XuZYWHOv035807ARP-ZLwFmA-_NxENJMxsisb-kUgkSr96fj5baBOZE="
     u"w24-h24-c-k-nd"},
    {u":face-purple-sweating:",
     u"https://yt3.ggpht.com/"
     u"tRnrCQtEKlTM9YLPo0vaxq9mDvlT0mhDld2KI7e_nDRbhta3ULKSoPVHZ1-bNlzQRANmH90="
     u"w24-h24-c-k-nd"},
    {u":face-purple-smiling-tears:",
     u"https://yt3.ggpht.com/"
     u"MJV1k3J5s0hcUfuo78Y6MKi-apDY5NVDjO9Q7hL8fU4i0cIBgU-"
     u"cU4rq4sHessJuvuGpDOjJ=w24-h24-c-k-nd"},
    {u":face-blue-star-eyes:",
     u"https://yt3.ggpht.com/"
     u"m_ANavMhp6cQ1HzX0HCTgp_er_yO2UA28JPbi-0HElQgnQ4_q5RUhgwueTpH-st8L3MyTA="
     u"w24-h24-c-k-nd"},
    {u":face-blue-heart-eyes:",
     u"https://yt3.ggpht.com/"
     u"M9tzKd64_r3hvgpTSgca7K3eBlGuyiqdzzhYPp7ullFAHMgeFoNLA0uQ1dGxj3fXgfcHW4w="
     u"w24-h24-c-k-nd"},
    {u":face-blue-three-eyes:", u"https://yt3.ggpht.com/"
                                u"nSQHitVplLe5uZC404dyAwv1f58S3PN-U_799fvFzq-"
                                u"6b3bv-MwENO-Zs1qQI4oEXCbOJg=w24-h24-c-k-nd"},
    {u":face-blue-droopy-eyes:",
     u"https://yt3.ggpht.com/"
     u"hGPqMUCiXGt6zuX4dHy0HRZtQ-vZmOY8FM7NOHrJTta3UEJksBKjOcoE6ZUAW9sz7gIF_nk="
     u"w24-h24-c-k-nd"},
    {u":planet-orange-purple-ring:",
     u"https://yt3.ggpht.com/"
     u"xkaLigm3P4_1g4X1JOtkymcC7snuJu_C5YwIFAyQlAXK093X0IUjaSTinMTLKeRZ6280jXg="
     u"w24-h24-c-k-nd"},
    {u":face-turquoise-speaker-shape:",
     u"https://yt3.ggpht.com/"
     u"WTFFqm70DuMxSC6ezQ5Zs45GaWD85Xwrd9Sullxt54vErPUKb_"
     u"o0NJQ4kna5m7rvjbRMgr3A=w24-h24-c-k-nd"},
    {u":octopus-red-waving:", u"https://yt3.ggpht.com/"
                              u"L9Wo5tLT_lRQX36iZO_fJqLJR4U74J77tJ6Dg-QmPmSC_"
                              u"zhVQ-NodMRc9T0ozwvRXRaT43o=w24-h24-c-k-nd"},
    {u":pillow-turquoise-hot-chocolate:",
     u"https://yt3.ggpht.com/"
     u"cAR4cehRxbn6dPbxKIb-"
     u"7ShDdWnMxbaBqy2CXzBW4aRL3IqXs3rxG0UdS7IU71OEU7LSd20q=w24-h24-c-k-nd"},
    {u":hourglass-purple-sand-orange:",
     u"https://yt3.ggpht.com/"
     u"MFDLjasPt5cuSM_tK5Fnjaz_k08lKHdX_Mf7JkI6awaHriC3rGL7J_wHxyG6PPhJ8CJ6vsQ="
     u"w24-h24-c-k-nd"},
    {u":fish-orange-wide-eyes:",
     u"https://yt3.ggpht.com/"
     u"iQLKgKs7qL3091VHgVgpaezc62uPewy50G_DoI0dMtVGmQEX5pflZrUxWfYGmRfzfUOOgJs="
     u"w24-h24-c-k-nd"},
    {u":popcorn-yellow-striped-smile:",
     u"https://yt3.ggpht.com/"
     u"TW_GktV5uVYviPDtkCRCKRDrGlUc3sJ5OHO81uqdMaaHrIQ5-sXXwJfDI3FKPyv4xtGpOlg="
     u"w24-h24-c-k-nd"},
    {u":penguin-blue-waving-tear:",
     u"https://yt3.ggpht.com/"
     u"p2u7dcfZau4_bMOMtN7Ma8mjHX_43jOjDwITf4U9adT44I-y-"
     u"PT7ddwPKkfbW6Wx02BTpNoC=w24-h24-c-k-nd"},
    {u":clock-turquoise-looking-up:",
     u"https://yt3.ggpht.com/"
     u"tDnDkDZykkJTrsWEJPlRF30rmbek2wcDcAIymruOvSLTsUFIZHoAiYTRe9OtO-80lDfFGvo="
     u"w24-h24-c-k-nd"},
    {u":face-red-smiling-live:",
     u"https://yt3.ggpht.com/"
     u"14Pb--7rVcqnHvM7UlrYnV9Rm4J-uojX1B1kiXYvv1my-eyu77pIoPR5sH28-eNIFyLaQHs="
     u"w24-h24-c-k-nd"},
    {u":hands-yellow-heart-red:",
     u"https://yt3.ggpht.com/"
     u"qWSu2zrgOKLKgt_E-XUP9e30aydT5aF3TnNjvfBL55cTu1clP8Eoh5exN3NDPEVPYmasmoA="
     u"w24-h24-c-k-nd"},
    {u":volcano-green-lava-orange:",
     u"https://yt3.ggpht.com/"
     u"_IWOdMxapt6IBY5Cb6LFVkA3J77dGQ7P2fuvYYv1-ahigpVfBvkubOuGLSCyFJ7jvis-X8I="
     u"w24-h24-c-k-nd"},
    {u":person-turquoise-waving-speech:",
     u"https://yt3.ggpht.com/"
     u"gafhCE49PH_9q-PuigZaDdU6zOKD6grfwEh1MM7fYVs7smAS_"
     u"yhYCBipq8gEiW73E0apKTzi=w24-h24-c-k-nd"},
    {u":face-orange-tv-shape:", u"https://yt3.ggpht.com/"
                                u"EVK0ik6dL5mngojX9I9Juw4iFh053emP0wcUjZH0whC_"
                                u"LabPq-DZxN4Jg-tpMcEVfJ0QpcJ4=w24-h24-c-k-nd"},
    {u":face-blue-spam-shape:",
     u"https://yt3.ggpht.com/"
     u"hpwvR5UgJtf0bGkUf8Rn-jTlD6DYZ8FPOFY7rhZZL-JHj_7OPDr7XUOesilRPxlf-aW42Zg="
     u"w24-h24-c-k-nd"},
    {u":face-fuchsia-flower-shape:",
     u"https://yt3.ggpht.com/"
     u"o9kq4LQ0fE_x8yxj29ZeLFZiUFpHpL_k2OivHbjZbttzgQytU49Y8-"
     u"VRhkOP18jgH1dQNSVz=w24-h24-c-k-nd"},
    {u":person-blue-holding-pencil:",
     u"https://yt3.ggpht.com/"
     u"TKgph5IHIHL-A3fgkrGzmiNXzxJkibB4QWRcf_kcjIofhwcUK_pWGUFC4xPXoimmne3h8eQ="
     u"w24-h24-c-k-nd"},
    {u":body-turquoise-yoga-pose:",
     u"https://yt3.ggpht.com/"
     u"GW3otW7CmWpuayb7Ddo0ux5c-OvmPZ2K3vaytJi8bHFjcn-ulT8vcHMNcqVqMp1j2lit2Vw="
     u"w24-h24-c-k-nd"},
    {u":location-yellow-teal-bars:",
     u"https://yt3.ggpht.com/"
     u"YgeWJsRspSlAp3BIS5HMmwtpWtMi8DqLg9fH7DwUZaf5kG4yABfE1mObAvjCh0xKX_"
     u"HoIR23=w24-h24-c-k-nd"},
    {u":person-turquoise-writing-headphones:",
     u"https://yt3.ggpht.com/"
     u"DC4KrwzNkVxLZa2_KbKyjZTUyB9oIvH5JuEWAshsMv9Ctz4lEUVK0yX5PaMsTK3gGS-r9w="
     u"w24-h24-c-k-nd"},
    {u":person-turquoise-wizard-wand:",
     u"https://yt3.ggpht.com/"
     u"OiZeNvmELg2PQKbT5UCS0xbmsGbqRBSbaRVSsKnRS9gvJPw7AzPp-3ysVffHFbSMqlWKeQ="
     u"w24-h24-c-k-nd"},
    {u":person-blue-eating-spaghetti:",
     u"https://yt3.ggpht.com/"
     u"AXZ8POmCHoxXuBaRxX6-xlT5M-nJZmO1AeUNo0t4o7xxT2Da2oGy347sHpMM8shtUs7Xxh0="
     u"w24-h24-c-k-nd"},
    {u":face-turquoise-music-note:",
     u"https://yt3.ggpht.com/"
     u"-K6oRITFKVU8V4FedrqXGkV_vTqUufVCQpBpyLK6w3chF4AS1kzT0JVfJxhtlfIAw5jrNco="
     u"w24-h24-c-k-nd"},
    {u":person-pink-swaying-hair:",
     u"https://yt3.ggpht.com/"
     u"L8cwo8hEoVhB1k1TopQaeR7oPTn7Ypn5IOae5NACgQT0E9PNYkmuENzVqS7dk2bYRthNAkQ="
     u"w24-h24-c-k-nd"},
    {u":person-blue-speaking-microphone:",
     u"https://yt3.ggpht.com/"
     u"FMaw3drKKGyc6dk3DvtHbkJ1Ki2uD0FLqSIiFDyuChc1lWcA9leahX3mCFMBIWviN2o8eyc="
     u"w24-h24-c-k-nd"},
    {u":rocket-red-countdown-liftoff:",
     u"https://yt3.ggpht.com/"
     u"lQZFYAeWe5-SJ_fz6dCAFYz1MjBnEek8DvioGxhlj395UFTSSHqYAmfhJN2i0rz3fDD5DQ="
     u"w24-h24-c-k-nd"},
    {u":face-purple-rain-drops:",
     u"https://yt3.ggpht.com/"
     u"woHW5Jl2RD0qxijnl_4vx4ZhP0Zp65D4Ve1DM_HrwJW-Kh6bQZoRjesGnEwjde8F4LynrQ="
     u"w24-h24-c-k-nd"},
    {u":face-pink-drinking-tea:",
     u"https://yt3.ggpht.com/"
     u"WRLIgKpnClgYOZyAwnqP-Edrdxu6_N19qa8gsB9P_6snZJYIMu5YBJX8dlM81YG6H307KA="
     u"w24-h24-c-k-nd"},
    {u":person-purple-stage-event:",
     u"https://yt3.ggpht.com/"
     u"YeVVscOyRcDJAhKo2bMwMz_B6127_"
     u"7lojqafTZECTR9NSEunYO5zEi7R7RqxBD7LYLxfNnXe=w24-h24-c-k-nd"},
    {u":face-purple-open-box:", u"https://yt3.ggpht.com/"
                                u"7lJM2sLrozPtNLagPTcN0xlcStWpAuZEmO2f4Ej5kYgSp"
                                u"3woGdq3tWFrTH30S3mD2PyjlQ=w24-h24-c-k-nd"},
    {u":person-yellow-podium-blue:",
     u"https://yt3.ggpht.com/"
     u"N28nFDm82F8kLPAa-jY_OySFsn3Ezs_2Bl5kdxC8Yxau5abkj_XZHYsS3uYKojs8qy8N-9w="
     u"w24-h24-c-k-nd"},
    {u":baseball-white-cap-out:",
     u"https://yt3.ggpht.com/"
     u"8DaGaXfaBN0c-ZsZ-1WqPJ6H9TsJOlUUQQEoXvmdROphZE9vdRtN0867Gb2YZcm2x38E9Q="
     u"w24-h24-c-k-nd"},
    {u":whistle-red-blow:",
     u"https://yt3.ggpht.com/"
     u"DBu1ZfPJTnX9S1RyKKdBY-X_CEmj7eF6Uzl71j5jVBz5y4k9JcKnoiFtImAbeu4u8M2X8tU="
     u"w24-h24-c-k-nd"},
    {u":person-turquoise-crowd-surf:",
     u"https://yt3.ggpht.com/"
     u"Q0wFvHZ5h54xGSTo-JeGst6InRU3yR6NdBRoyowaqGY66LPzdcrV2t-wBN21kBIdb2TeNA="
     u"w24-h24-c-k-nd"},
    {u":finger-red-number-one:",
     u"https://yt3.ggpht.com/"
     u"Hbk0wxBzPTBCDvD_y4qdcHL5_"
     u"uu7SeOnaT2B7gl9GLB4u8Ecm9OaXCGSMMUBFeNGl5Q3fHJ2=w24-h24-c-k-nd"},
    {u":text-yellow-goal:", u"https://yt3.ggpht.com/"
                            u"tnHp8rHjXecGbGrWNcs7xss_aVReaYE6H-QWRCXYg_"
                            u"aaYszHXnbP_pVADnibUiimspLvgX0L=w24-h24-c-k-nd"},
    {u":medal-yellow-first-red:",
     u"https://yt3.ggpht.com/"
     u"EEHiiIalCBKuWDPtNOjjvmEZ-KRkf5dlgmhe5rbLn8aZQl-pNz_paq5UjxNhCrI019TWOQ="
     u"w24-h24-c-k-nd"},
    {u":person-blue-wheelchair-race:",
     u"https://yt3.ggpht.com/"
     u"ZepxPGk5TwzrKAP9LUkzmKmEkbaF5OttNyybwok6mJENw3p0lxDXkD1X2_rAwGcUM0L-D04="
     u"w24-h24-c-k-nd"},
    {u":card-red-penalty:",
     u"https://yt3.ggpht.com/"
     u"uRDUMIeAHnNsaIaShtRkQ6hO0vycbNH_BQT7i3PWetFJb09q88RTjxwzToBy9Cez20D7hA="
     u"w24-h24-c-k-nd"},
    {u":stopwatch-blue-hand-timer:",
     u"https://yt3.ggpht.com/"
     u"DCvefDAiskRfACgolTlvV1kMfiZVcG50UrmpnRrg3k0udFWG2Uo9zFMaJrJMSJYwcx6fMgk="
     u"w24-h24-c-k-nd"},

    {u":awesome:",
     u"data:image/jpeg;base64,/9j/4AAQSkZJRgABAQEAYABgAAD/"
     u"2wCEAAgICAgJCAkKCgkNDgwODRMREBARExwUFhQWFBwrGx8bGx8bKyYuJSMlLiZENS8vNURO"
     u"Qj5CTl9VVV93cXecnNEBCAgICAkICQoKCQ0ODA4NExEQEBETHBQWFBYUHCsbHxsbHxsrJi4l"
     u"IyUuJkQ1Ly81RE5CPkJOX1VVX3dxd5yc0f/CABEIABgAGAMBIgACEQEDEQH/"
     u"xAAYAAEAAwEAAAAAAAAAAAAAAAAGAAEEAv/aAAgBAQAAAABzcMd69JJp/"
     u"8QAFgEBAQEAAAAAAAAAAAAAAAAABAMG/9oACAECEAAAAM+tF//"
     u"EABYBAQEBAAAAAAAAAAAAAAAAAAMEBf/aAAgBAxAAAADQBJf/"
     u"xAAoEAACAgEDAgQHAAAAAAAAAAACAwEEBQAREgYUEyEyYiJBQlFSksH/"
     u"2gAIAQEAAT8Ay+fCw6141ttTE1nRXY5cyuWu347SyPMAifx0zB4ICAYw6Dcc7BwTEtnb3/"
     u"2Z1hc4C+2ZXvzbxT39tyYUsZWdJcYjnPmQEWlZmph8NjE2ZsbXKq2nApU0ZatYrP1/"
     u"OJjUdcYdbENR3yzSk1RPhLLcS4/"
     u"efZqcjTyXTWftVJPeQaOxrFezYXEjtAa6r6YcYPgFsOobzsLNQSw6zj9cSEeZrPVCl0ymnKL"
     u"XbPuEPxnNsBHf2RE84/TXTPTjRVUTKmhQS+LJm8JW208fTPD6ADbX/"
     u"8QAIBEAAgEEAQUAAAAAAAAAAAAAAQIDAAQRElETITFBgf/"
     u"aAAgBAgEBPwC4WW4miZWn0yQ+vgY4q1uZMSIxzo5AL9mxUjNCzdM42BP2oY1ROSTkk+zX/"
     u"8QAHBEAAgMBAAMAAAAAAAAAAAAAAQMAAhEhBCJR/9oACAEDAQE/"
     u"AFJaFk1Vvw5GJGjfU50RPkuos1FuRl7WsST2f//Z"},
    {u":gar:",
     u"data:image/jpeg;base64,/9j/4AAQSkZJRgABAQEAYABgAAD/"
     u"2wCEAAgICAgJCAkKCgkNDgwODRMREBARExwUFhQWFBwrGx8bGx8bKyYuJSMlLiZENS8vNURO"
     u"Qj5CTl9VVV93cXecnNEBCAgICAkICQoKCQ0ODA4NExEQEBETHBQWFBYUHCsbHxsbHxsrJi4l"
     u"IyUuJkQ1Ly81RE5CPkJOX1VVX3dxd5yc0f/CABEIABgAGAMBIgACEQEDEQH/"
     u"xAAXAAADAQAAAAAAAAAAAAAAAAAEBQYH/9oACAEBAAAAANlUMpdNVnCGf//"
     u"EABYBAQEBAAAAAAAAAAAAAAAAAAQDBv/aAAgBAhAAAADCVc//"
     u"xAAWAQEBAQAAAAAAAAAAAAAAAAADBAX/2gAIAQMQAAAA2jKf/"
     u"8QAJRAAAQQCAQMEAwAAAAAAAAAAAgEDBBEABSESIjETFCOBFTNB/9oACAEBAAE/"
     u"ANvuY2sZJx1wARKsjWhG8XaSzo0e4XlK8c5A2hm4jb9L1eCzcttzmnxfi+"
     u"5EyRfSurpeOVrxkuKxuAjxZUSZGRp4XhTsr4/4pAppWCx8APdXcr3SifWSNQ+hKrVEOfjpl/"
     u"qyNqH1MSdoUT7XP//EAB8RAAIBBQADAQAAAAAAAAAAAAECBAADBRETMUFCYf/"
     u"aAAgBAgEBPwCVkbstyyyHt21cjSn59HVRszOj2lS4nT2GPnRqRgoPUlei7PgGo2GgJaA5lv1"
     u"jX//EAB0RAAICAwADAAAAAAAAAAAAAAECAxEABBIxQXH/"
     u"2gAIAQMBAT8Ai1kiWigZiB5yXUgkclWC+iMi3Z+aNHH252a+q+Z//9k="},
    {u":jakepeter:",
     u"data:image/jpeg;base64,/9j/4AAQSkZJRgABAQEAYABgAAD/"
     u"2wCEAAgICAgJCAkKCgkNDgwODRMREBARExwUFhQWFBwrGx8bGx8bKyYuJSMlLiZENS8vNURO"
     u"Qj5CTl9VVV93cXecnNEBCAgICAkICQoKCQ0ODA4NExEQEBETHBQWFBYUHCsbHxsbHxsrJi4l"
     u"IyUuJkQ1Ly81RE5CPkJOX1VVX3dxd5yc0f/CABEIABgAGAMBIgACEQEDEQH/"
     u"xAAXAAEBAQEAAAAAAAAAAAAAAAAHBgQI/9oACAEBAAAAAFkK6JkDi83TSn//"
     u"xAAVAQEBAAAAAAAAAAAAAAAAAAAAAf/aAAgBAhAAAABY/"
     u"8QAFQEBAQAAAAAAAAAAAAAAAAAAAwT/2gAIAQMQAAAAqjY//"
     u"8QAJhAAAgICAQMDBQEAAAAAAAAAAQIDBAAFBgcREiEiMRNBQlFSU//"
     u"aAAgBAQABPwDlXI6nHtabM7gM5KRj9tmu5HyaXYRyjlaySCdA8EwCK/"
     u"r6qua661qNw69mT5Izm+vo3pdebdp6wigtOsqfifZlrpUkpR6mxHisfvDf6Z0uS5DrbtW6/"
     u"wBRIZAIXBJ7oc6gQ20XXXIa8k1OMTR2xEC7qj+"
     u"LCQAfyUzVX7O7K0dPRtQQeJSW9LEY44gPkRAk93zUaXWaWmtTX1lhgBJ8ASSSfuSc/"
     u"8QAGhEBAAIDAQAAAAAAAAAAAAAAARESAAIDEP/aAAgBAgEBPwAJc6VdmpB7/"
     u"8QAHBEAAgICAwAAAAAAAAAAAAAAAQIDEQAQEhNB/9oACAEDAQE/"
     u"AIuAia4wS3tkEYgZVpzZwMevX//Z"},
    {u":wormOrangeGreen:",
     u"data:image/jpeg;base64,/9j/4AAQSkZJRgABAQEAYABgAAD/"
     u"2wCEAAgICAgJCAkKCgkNDgwODRMREBARExwUFhQWFBwrGx8bGx8bKyYuJSMlLiZENS8vNURO"
     u"Qj5CTl9VVV93cXecnNEBCAgICAkICQoKCQ0ODA4NExEQEBETHBQWFBYUHCsbHxsbHxsrJi4l"
     u"IyUuJkQ1Ly81RE5CPkJOX1VVX3dxd5yc0f/CABEIABgAGAMBIgACEQEDEQH/"
     u"xAAYAAACAwAAAAAAAAAAAAAAAAAFBgIEB//aAAgBAQAAAADd0cq0KYmVI3//"
     u"xAAWAQEBAQAAAAAAAAAAAAAAAAAEAgP/2gAIAQIQAAAADnLv/"
     u"8QAFgEBAQEAAAAAAAAAAAAAAAAABAEF/9oACAEDEAAAAGzCX//"
     u"EACQQAAICAQMEAgMAAAAAAAAAAAECAwQRAAUSEyExQSIjUWFx/9oACAEBAAE/"
     u"ALdgV600xGQiE4/OnNi03KVpJX4liBnCj9D1rZ78iTpXklLxSD4Fjkgj1nVyKO1XlhY4DqV/"
     u"mdVNt3KO7AWTgkZ+UoYYZR6x576kmiisP02GI7H147+"
     u"G1uEG7JuFh2Fs5dum8XJgEPgDhrnuEoKF70mVOV+xtbRslgzQ2LKCOOPBSP2WHgnX/"
     u"8QAHBEAAgMBAAMAAAAAAAAAAAAAAQIAAxEEEkFR/9oACAECAQE/"
     u"AOtLHetRvh7yUdDIpRtJVs2W15YQHfCfspqREAAn/"
     u"8QAHxEAAgIBBAMAAAAAAAAAAAAAAQIAAxEEEhQhMTJR/9oACAEDAQE/"
     u"ANJZVWljHbvHjM1HGdw6W1jI7GZamH927P2VVIiAAT//2Q=="},
    {u":wormRedBlue:",
     u"data:image/jpeg;base64,/9j/4AAQSkZJRgABAQEAYABgAAD/"
     u"2wCEAAgICAgJCAkKCgkNDgwODRMREBARExwUFhQWFBwrGx8bGx8bKyYuJSMlLiZENS8vNURO"
     u"Qj5CTl9VVV93cXecnNEBCAgICAkICQoKCQ0ODA4NExEQEBETHBQWFBYUHCsbHxsbHxsrJi4l"
     u"IyUuJkQ1Ly81RE5CPkJOX1VVX3dxd5yc0f/CABEIABgAGAMBIgACEQEDEQH/"
     u"xAAZAAACAwEAAAAAAAAAAAAAAAAEBQEDBgf/2gAIAQEAAAAA7ipUqCLJ153/"
     u"xAAXAQADAQAAAAAAAAAAAAAAAAABAgQF/9oACAECEAAAAGuzj//"
     u"EABYBAQEBAAAAAAAAAAAAAAAAAAYEBf/aAAgBAxAAAACwurz/AP/"
     u"EACkQAAICAgEBBQkAAAAAAAAAAAECAwQFEQAxEiIyUVIGIUFDYXGhscL/"
     u"2gAIAQEAAT8Au5CpQjElmUKD0HUn7DkuUrZWpYr05+"
     u"zYZDpG7jcw9wVLEUYOoZj2SnpfmUnls5S0S42shhQHooHd/"
     u"PLuLnx8ytLJuPW45lGtN10fL6crrJbuxIjdp2kDuw+ADbJ57R0WrW5LHyJh72A8La/"
     u"rmIkmtYqu1pNuyEMGHiG9AkHzHIKteAEQwpGD6FC/rn//"
     u"xAAdEQACAgIDAQAAAAAAAAAAAAABAgADETEEEyFB/9oACAECAQE/"
     u"AKgz2qq439nLSrsGWAJHs00yTP/EAB0RAAICAgMBAAAAAAAAAAAAAAECAwQAEgUTIXH/"
     u"2gAIAQMBAT8At16tag0s0jbtGWAXOPnsdB1UsoY6k5PIZKNfYAkMVHzAAPAM/9k="},
    {u":wormYellowRed:",
     u"data:image/jpeg;base64,/9j/4AAQSkZJRgABAQEAYABgAAD/"
     u"2wCEAAgICAgJCAkKCgkNDgwODRMREBARExwUFhQWFBwrGx8bGx8bKyYuJSMlLiZENS8vNURO"
     u"Qj5CTl9VVV93cXecnNEBCAgICAkICQoKCQ0ODA4NExEQEBETHBQWFBYUHCsbHxsbHxsrJi4l"
     u"IyUuJkQ1Ly81RE5CPkJOX1VVX3dxd5yc0f/CABEIABgAGAMBIgACEQEDEQH/"
     u"xAAYAAACAwAAAAAAAAAAAAAAAAAGBwIEBf/aAAgBAQAAAABukQxLbWLPA7P/"
     u"xAAWAQEBAQAAAAAAAAAAAAAAAAAFAwT/2gAIAQIQAAAAIVjo/"
     u"8QAFgEBAQEAAAAAAAAAAAAAAAAAAwQF/9oACAEDEAAAAH0ZC//"
     u"EACsQAAICAAMGBAcAAAAAAAAAAAECAwQABRIREyEiMUEGEDJhM1JxcpHR4f/"
     u"aAAgBAQABPwBLdwWVkM3KbG7MewbAC2n8jyo+J69u2sJhKJJ8Jyw5vqO2LMJWezD01HeIfu/"
     u"uKlpLMQYEahwdflOLwjiuWRV0hUmO69mB/"
     u"fTFyoliNGXhInENjMa+ZJb1CtMDsI1QhiGHuVxkmR2GnhsWYRHFFzIh9TN2JGP/"
     u"xAAeEQABBAIDAQAAAAAAAAAAAAABAAIDEQQTBRIiUf/"
     u"aAAgBAgEBPwDjp37sqCV5L2vsX8QcjjQmbd191VoCl//"
     u"EAB8RAAIBBAIDAAAAAAAAAAAAAAECAwAEERIFIRMxYf/"
     u"aAAgBAwEBPwCVRqjAdYqXjkbxtG2oZAcGrOZo3wACPoq6uJZZSzH11X//2Q=="},
    {u":ytg:",
     u"data:image/jpeg;base64,/9j/4AAQSkZJRgABAQEAYABgAAD/"
     u"2wBDAAgICAgJCAkKCgkNDgwODRMREBARExwUFhQWFBwrGx8bGx8bKyYuJSMlLiZENS8vNURO"
     u"Qj5CTl9VVV93cXecnNH/wgALCAAYABgBAREA/"
     u"8QAGQAAAgMBAAAAAAAAAAAAAAAAAAUCAwYH/9oACAEBAAAAAO/FKRnnNkR//"
     u"8QAJBAAAgEDAwQDAQAAAAAAAAAAAQMCAAQRBRJBEyIxUSFCccH/"
     u"2gAIAQEAAT8A3zkMwGB7PNNu1qA3xmCfAx/"
     u"fFJ1SJJDY7R7Fa7ol7rCtOFpfztzb3KWMh2fQ5390J98fMeK1SDIItl9YyYFyHUkBkn47iBi"
     u"rGweuUmuuWvumpXBmMhZkseVr+uealEH991cWbLiSt8xtjnJ5+aWpao4gP08mv//Z"},
    {u":yt:", u"https://yt3.ggpht.com/"
              u"IkpeJf1g9Lq0WNjvSa4XFq4LVNZ9IP5FKW8yywXb12djo1OGdJtziejNASITyq4"
              u"L0itkMNw=w24-h24-c-k-nd"},
    {u":oops:", u"https://yt3.ggpht.com/"
                u"PFoVIqIiFRS3aFf5-bt_tTC0WrDm_ylhF4BKKwgqAASNb7hVgx_adFP-"
                u"XVhFiJLXdRK0EQ=w24-h24-c-k-nd"},
    {u":buffering:",
     u"https://yt3.ggpht.com/"
     u"5gfMEfdqO9CiLwhN9Mq7VI6--T2QFp8AXNNy5Fo7btfY6fRKkThWq35SCZ6SPMVCjg-sUA="
     u"w24-h24-c-k-nd"},
    {u":stayhome:", u"https://yt3.ggpht.com/"
                    u"_1FGHypiub51kuTiNBX1a0H3NyFih3TnHX7bHU06j_"
                    u"ajTzT0OQfMLl9RI1SiQoxtgA2Grg=w24-h24-c-k-nd"},
    {u":dothefive:", u"https://yt3.ggpht.com/"
                     u"-nM0DOd49969h3GNcl705Ti1fIf1ZG_E3JxcOUVV-"
                     u"qPfCW6jY8xZ98caNLHkVSGRTSEb7Y9y=w24-h24-c-k-nd"},
    {u":elbowbump:", u"https://yt3.ggpht.com/"
                     u"2ou58X5XuhTrxjtIM2wew1f-HKRhN_T5SILQgHE-"
                     u"WD9dySzzJdGwL4R1gpKiJXcbtq6sjQ=w24-h24-c-k-nd"},
    {u":goodvibes:", u"https://yt3.ggpht.com/"
                     u"2CvFOwgKpL29mW_C51XvaWa7Eixtv-3tD1XvZa1_"
                     u"WemaDDL2AqevKbTZ1rdV0OWcnOZRag=w24-h24-c-k-nd"},
    {u":thanksdoc:", u"https://yt3.ggpht.com/"
                     u"bUnO_VwXW2hDf-Da8D64KKv6nBJDYUBuo13RrOg141g2da8pi9-"
                     u"KClJYlUDuqIwyPBfvOO8=w24-h24-c-k-nd"},
    {u":videocall:",
     u"https://yt3.ggpht.com/"
     u"k5v_oxUzRWmTOXP0V6WJver6xdS1lyHMPcMTfxn23Md6rmixoR5RZUusFbZi1uZwjF__pv4="
     u"w24-h24-c-k-nd"},
    {u":virtualhug:", u"https://yt3.ggpht.com/"
                      u"U1TjOZlqtS58NGqQhE8VWDptPSrmJNkrbVRp_"
                      u"8jI4f84QqIGflq2Ibu7YmuOg5MmVYnpevc=w24-h24-c-k-nd"},
    {u":yougotthis:", u"https://yt3.ggpht.com/"
                      u"s3uOe4lUx3iPIt1h901SlMp_sKCTp3oOVj1JV8izBw_vDVLxFqk5dq-"
                      u"3NX-nK_gnUwVEXld3=w24-h24-c-k-nd"},
    {u":sanitizer:", u"https://yt3.ggpht.com/"
                     u"EJ_8vc4Gl-WxCWBurHwwWROAHrPzxgePodoNfkRY1U_I8L1O2zlqf7-"
                     u"wfUtTeyzq2qHNnocZ=w24-h24-c-k-nd"},
    {u":takeout:", u"https://yt3.ggpht.com/"
                   u"FizHI5IYMoNql9XeP7TV3E0ffOaNKTUSXbjtJe90e1OUODJfZbWU37VqBb"
                   u"Th-vpyFHlFIS0=w24-h24-c-k-nd"},
    {u":hydrate:", u"https://yt3.ggpht.com/"
                   u"tpgZgmhX8snKniye36mnrDVfTnlc44EK92EPeZ0m9M2EPizn1vKEGJzNYd"
                   u"p7KQy6iNZlYDc1=w24-h24-c-k-nd"},
    {u":chillwcat:", u"https://yt3.ggpht.com/"
                     u"y03dFcPc1B7CO20zgQYzhcRPka5Bhs6iSg57MaxJdhaLidFvvXBLf_"
                     u"i4_SHG7zJ_2VpBMNs=w24-h24-c-k-nd"},
    {u":chillwdog:", u"https://yt3.ggpht.com/"
                     u"Ir9mDxzUi0mbqyYdJ3N9Lq7bN5Xdt0Q7fEYFngN3GYAcJT_"
                     u"tccH1as1PKmInnpt2cbWOam4=w24-h24-c-k-nd"},
    {u":elbowcough:", u"https://yt3.ggpht.com/"
                      u"DTR9bZd1HOqpRJyz9TKiLb0cqe5Hb84Yi_79A6LWlN1tY-"
                      u"5kXqLDXRmtYVKE9rcqzEghmw=w24-h24-c-k-nd"},
    {u":learning:", u"https://yt3.ggpht.com/"
                    u"ZuBuz8GAQ6IEcQc7CoJL8IEBTYbXEvzhBeqy1AiytmhuAT0VHjpXEjd-"
                    u"A5GfR4zDin1L53Q=w24-h24-c-k-nd"},
    {u":washhands:", u"https://yt3.ggpht.com/"
                     u"qXUeUW0KpKBc9Z3AqUqr_0B7HbW1unAv4qmt7-LJGUK_"
                     u"gsFBIaHISWJNt4n3yvmAnQNZHE-u=w24-h24-c-k-nd"},
    {u":socialdist:", u"https://yt3.ggpht.com/"
                      u"igBNi55-TACUi1xQkqMAor-IEXmt8He56K7pDTG5XoTsbM-"
                      u"rVswNzUfC5iwnfrpunWihrg=w24-h24-c-k-nd"},
    {u":shelterin:", u"https://yt3.ggpht.com/"
                     u"gjC5x98J4BoVSEPfFJaoLtc4tSBGSEdIlfL2FV4iJG9uGNykDP9oJC_"
                     u"QxAuBTJy6dakPxVeC=w24-h24-c-k-nd"},
});

enum class CacheState : std::uint8_t { Loading, Ready, Failed };

struct CacheEntry {
    CacheState state = CacheState::Loading;
    std::weak_ptr<const YouTubeLiveChatPage> page;
    QString error;
    std::vector<YouTubeEmotes::PageCallback> callbacks;
    std::uint64_t lastUse = 0;
    std::chrono::steady_clock::time_point retryAfter{};
};

struct ParseResult {
    YouTubeEmotes::PagePtr page;
    QString error;
    bool recognized = false;
};

struct LoaderState {
    std::unordered_map<QString, CacheEntry> cache;
    QPointer<QNetworkAccessManager> manager;
    std::uint64_t useCounter = 0;
    std::size_t inFlight = 0;
};

struct RequestState {
    QByteArray data;
    bool rejected = false;
    bool timedOut = false;
    bool finished = false;
};

LoaderState &loaderState()
{
    static LoaderState state;
    return state;
}

QNetworkAccessManager *networkManager()
{
    auto &state = loaderState();
    if (!state.manager)
    {
        state.manager = new QNetworkAccessManager(QCoreApplication::instance());
    }
    return state.manager;
}

std::shared_ptr<const EmoteMap> makeGlobalEmotes()
{
    auto emotes = std::make_shared<EmoteMap>();
    emotes->reserve(GLOBAL_EMOTES.size());
    for (const auto &entry : GLOBAL_EMOTES)
    {
        const auto shortcut = entry.shortcut.toString();
        const EmoteName name{shortcut};
        emotes->emplace(name,
                        YouTubeEmotes::makeCustomEmoji(
                            shortcut, entry.imageUrl.toString(), shortcut));
    }
    return emotes;
}

bool isValidVideoID(QStringView videoID)
{
    if (videoID.size() != 11)
    {
        return false;
    }
    return std::ranges::all_of(videoID, [](QChar c) {
        const auto value = c.unicode();
        return (value >= 'A' && value <= 'Z') ||
               (value >= 'a' && value <= 'z') ||
               (value >= '0' && value <= '9') || value == '_' || value == '-';
    });
}

void invokeLater(YouTubeEmotes::PageCallback callback,
                 ExpectedStr<YouTubeEmotes::PagePtr> result)
{
    if (!callback)
    {
        return;
    }
    if (auto *app = QCoreApplication::instance())
    {
        QTimer::singleShot(
            0, app,
            [callback = std::move(callback), result = std::move(result)] {
                callback(std::move(result));
            });
        return;
    }
    callback(std::move(result));
}

void trimCache()
{
    auto &cache = loaderState().cache;
    while (cache.size() >= MAX_CACHE_ENTRIES)
    {
        auto oldest = cache.end();
        for (auto it = cache.begin(); it != cache.end(); ++it)
        {
            if (it->second.state == CacheState::Loading)
            {
                continue;
            }
            if (oldest == cache.end() ||
                it->second.lastUse < oldest->second.lastUse)
            {
                oldest = it;
            }
        }
        if (oldest == cache.end())
        {
            return;
        }
        cache.erase(oldest);
    }
}

void finishLoad(const QString &videoID, ParseResult parsed)
{
    auto &state = loaderState();
    if (state.inFlight > 0)
    {
        --state.inFlight;
    }

    const auto it = state.cache.find(videoID);
    if (it == state.cache.end())
    {
        return;
    }

    auto callbacks = std::move(it->second.callbacks);
    const bool succeeded = parsed.recognized && parsed.page != nullptr &&
                           parsed.error.trimmed().isEmpty();
    it->second.state = succeeded ? CacheState::Ready : CacheState::Failed;
    if (succeeded)
    {
        it->second.page = parsed.page;
    }
    else
    {
        it->second.page.reset();
    }
    it->second.error = parsed.error.trimmed().isEmpty()
                           ? u"YouTube did not expose live chat page data."_s
                           : std::move(parsed.error);
    it->second.lastUse = ++state.useCounter;
    it->second.retryAfter =
        succeeded ? std::chrono::steady_clock::time_point{}
                  : std::chrono::steady_clock::now() + FAILURE_CACHE_TTL;
    const auto page = std::move(parsed.page);
    const auto error = it->second.error;

    for (auto &callback : callbacks)
    {
        if (callback)
        {
            if (succeeded && page)
            {
                callback(page);
            }
            else
            {
                callback(makeUnexpected(error));
            }
        }
    }
}

std::optional<QByteArray> extractBalancedObjectAt(const QByteArray &html,
                                                  qsizetype jsonStart)
{
    if (jsonStart < 0 || jsonStart >= html.size() || html.at(jsonStart) != '{')
    {
        return std::nullopt;
    }

    int depth = 0;
    bool inString = false;
    bool escaped = false;
    for (qsizetype i = jsonStart; i < html.size(); ++i)
    {
        const auto c = html.at(i);
        if (escaped)
        {
            escaped = false;
            continue;
        }
        if (inString && c == '\\')
        {
            escaped = true;
            continue;
        }
        if (c == '"')
        {
            inString = !inString;
            continue;
        }
        if (inString)
        {
            continue;
        }
        if (c == '{')
        {
            ++depth;
        }
        else if (c == '}')
        {
            --depth;
            if (depth == 0)
            {
                return html.mid(jsonStart, i - jsonStart + 1);
            }
            if (depth < 0)
            {
                return std::nullopt;
            }
        }
    }
    return std::nullopt;
}

std::optional<QByteArray> extractInitialData(const QByteArray &html)
{
    static constexpr std::array<QByteArrayView, 6> MARKERS{
        "var ytInitialData =",       "window[\"ytInitialData\"] =",
        "window['ytInitialData'] =", "ytInitialData\"] =",
        "ytInitialData'] =",         "ytInitialData =",
    };

    qsizetype jsonStart = -1;
    for (const auto marker : MARKERS)
    {
        const auto markerStart = html.indexOf(marker);
        if (markerStart >= 0)
        {
            jsonStart = markerStart + marker.size();
            break;
        }
    }
    if (jsonStart < 0)
    {
        return std::nullopt;
    }

    while (jsonStart < html.size() &&
           std::isspace(static_cast<unsigned char>(html.at(jsonStart))))
    {
        ++jsonStart;
    }
    return extractBalancedObjectAt(html, jsonStart);
}

std::optional<QByteArray> extractBalancedObject(const QByteArray &html,
                                                qsizetype searchFrom)
{
    const auto jsonStart = html.indexOf('{', searchFrom);
    return extractBalancedObjectAt(html, jsonStart);
}

QJsonObject extractYtConfig(const QByteArray &html)
{
    static constexpr QByteArrayView MARKER{"ytcfg.set("};
    QJsonObject merged;
    qsizetype searchFrom = 0;
    std::size_t objects = 0;
    while (searchFrom < html.size() && objects < 32)
    {
        const auto markerAt = html.indexOf(MARKER, searchFrom);
        if (markerAt < 0)
        {
            break;
        }
        const auto json = extractBalancedObject(
            html, markerAt + static_cast<qsizetype>(MARKER.size()));
        searchFrom = markerAt + static_cast<qsizetype>(MARKER.size());
        if (!json)
        {
            continue;
        }
        searchFrom =
            markerAt + static_cast<qsizetype>(MARKER.size()) + json->size();
        ++objects;

        QJsonParseError error;
        const auto document = QJsonDocument::fromJson(*json, &error);
        if (error.error != QJsonParseError::NoError || !document.isObject())
        {
            continue;
        }
        const auto object = document.object();
        for (auto it = object.begin(); it != object.end(); ++it)
        {
            merged.insert(it.key(), it.value());
        }
    }
    return merged;
}

QString configString(const QJsonObject &config, QStringView key)
{
    const auto value = config.value(key);
    if (value.isString())
    {
        return value.toString().trimmed();
    }
    if (value.isDouble())
    {
        return QString::number(value.toInteger());
    }
    return {};
}

QString continuationToken(const QJsonObject &container,
                          QString *clickTracking = nullptr)
{
    static constexpr std::array<QStringView, 4> KINDS{
        u"reloadContinuationData", u"invalidationContinuationData",
        u"timedContinuationData", u"liveChatReplayContinuationData"};
    for (const auto kind : KINDS)
    {
        const auto data = container.value(kind).toObject();
        const auto token = data.value(u"continuation"_s).toString();
        if (token.isEmpty())
        {
            continue;
        }
        if (clickTracking)
        {
            *clickTracking = data.value(u"clickTrackingParams"_s).toString();
            if (clickTracking->isEmpty())
            {
                *clickTracking = data.value(u"trackingParams"_s).toString();
            }
        }
        return token;
    }
    return {};
}

QString liveChatContinuation(const QJsonObject &initialData,
                             QString *clickTracking)
{
    const auto renderer = initialData.value(u"contents"_s)
                              .toObject()
                              .value(u"liveChatRenderer"_s)
                              .toObject();
    const auto menu = renderer.value(u"header"_s)
                          .toObject()
                          .value(u"liveChatHeaderRenderer"_s)
                          .toObject()
                          .value(u"viewSelector"_s)
                          .toObject()
                          .value(u"sortFilterSubMenuRenderer"_s)
                          .toObject()
                          .value(u"subMenuItems"_s)
                          .toArray();

    if (menu.size() >= 2)
    {
        const auto token = continuationToken(
            menu.at(1).toObject().value(u"continuation"_s).toObject(),
            clickTracking);
        if (!token.isEmpty())
        {
            return token;
        }
    }
    for (const auto &value : menu)
    {
        const auto token = continuationToken(
            value.toObject().value(u"continuation"_s).toObject(),
            clickTracking);
        if (!token.isEmpty())
        {
            return token;
        }
    }
    for (const auto &value : renderer.value(u"continuations"_s).toArray())
    {
        const auto token = continuationToken(value.toObject(), clickTracking);
        if (!token.isEmpty())
        {
            return token;
        }
    }
    return {};
}

bool validBounded(QStringView value, qsizetype maximum)
{
    return !value.isEmpty() && value.size() <= maximum;
}

QString bestThumbnail(const QJsonArray &thumbnails)
{
    QString best;
    qint64 bestArea = -1;
    for (const auto &thumbnailValue : thumbnails)
    {
        const auto thumbnail = thumbnailValue.toObject();
        const auto url = thumbnail.value(u"url"_s).toString();
        if (!isTrustedYouTubeImageUrl(url))
        {
            continue;
        }

        const auto width = std::max(1, thumbnail.value(u"width"_s).toInt());
        const auto height = std::max(1, thumbnail.value(u"height"_s).toInt());
        const auto area = static_cast<qint64>(width) * height;
        if (area >= bestArea)
        {
            bestArea = area;
            best = url;
        }
    }
    return best;
}

QString normalizeShortcut(QString shortcut)
{
    shortcut = shortcut.trimmed();
    if (shortcut.isEmpty() || shortcut.size() > 80)
    {
        return {};
    }
    if (!shortcut.startsWith(u':') || !shortcut.endsWith(u':'))
    {
        if (shortcut.contains(u':') ||
            std::ranges::any_of(shortcut, [](QChar c) {
                return c.isSpace();
            }))
        {
            return {};
        }
        shortcut = u':' + shortcut + u':';
    }
    if (shortcut.size() <= 2 ||
        std::ranges::any_of(shortcut.sliced(1, shortcut.size() - 2),
                            [](QChar c) {
                                return c.isSpace() || c == u'<' || c == u'>';
                            }))
    {
        return {};
    }
    return shortcut;
}

void collectEmojiObject(const QJsonObject &object, EmoteMap &emotes,
                        std::size_t &shortcutCount)
{
    if (emotes.size() >= MAX_SHORTCUTS || shortcutCount >= MAX_SHORTCUTS)
    {
        return;
    }

    const auto emojiID = object.value(u"emojiId"_s).toString().trimmed();
    if (emojiID.isEmpty())
    {
        return;
    }

    const auto image = object.value(u"image"_s).toObject();
    const auto imageUrl = bestThumbnail(image.value(u"thumbnails"_s).toArray());
    if (imageUrl.isEmpty())
    {
        return;
    }

    if (!object.value(u"isCustomEmoji"_s).toBool() && !emojiID.contains(u'/'))
    {
        return;
    }

    QStringList shortcuts;
    QSet<QString> seenShortcuts;
    for (const auto &value : object.value(u"shortcuts"_s).toArray())
    {
        if (shortcuts.size() >= MAX_SHORTCUTS - emotes.size())
        {
            break;
        }
        auto shortcut = normalizeShortcut(value.toString());
        if (!shortcut.isEmpty() && !emotes.contains(EmoteName{shortcut}) &&
            !seenShortcuts.contains(shortcut))
        {
            seenShortcuts.insert(shortcut);
            shortcuts.emplace_back(std::move(shortcut));
        }
    }
    if (shortcuts.isEmpty())
    {
        auto fallback = normalizeShortcut(emojiID.section(u'/', -1));
        if (!fallback.isEmpty())
        {
            shortcuts.emplace_back(std::move(fallback));
        }
    }
    if (shortcuts.isEmpty())
    {
        return;
    }

    for (const auto &shortcut : shortcuts)
    {
        if (shortcutCount >= MAX_SHORTCUTS || emotes.size() >= MAX_SHORTCUTS)
        {
            break;
        }
        const EmoteName name{shortcut};
        if (emotes.contains(name))
        {
            continue;
        }

        auto emote =
            YouTubeEmotes::makeCustomEmoji(shortcut, imageUrl, emojiID);
        emotes.emplace(name, std::move(emote));
        ++shortcutCount;
    }
}

void collectEmojiObjects(const QJsonValue &value, EmoteMap &emotes,
                         std::size_t &nodeCount, std::size_t &emojiCount,
                         std::size_t &shortcutCount, int depth)
{
    if (depth > MAX_JSON_DEPTH || nodeCount >= MAX_JSON_NODES ||
        emojiCount >= MAX_EMOTES || shortcutCount >= MAX_SHORTCUTS)
    {
        return;
    }
    ++nodeCount;

    if (value.isObject())
    {
        const auto object = value.toObject();
        if (object.contains(u"emojiId"_s))
        {
            const auto before = shortcutCount;
            collectEmojiObject(object, emotes, shortcutCount);
            if (shortcutCount != before)
            {
                ++emojiCount;
            }
        }
        for (auto it = object.begin(); it != object.end(); ++it)
        {
            collectEmojiObjects(it.value(), emotes, nodeCount, emojiCount,
                                shortcutCount, depth + 1);
            if (nodeCount >= MAX_JSON_NODES || emojiCount >= MAX_EMOTES ||
                shortcutCount >= MAX_SHORTCUTS)
            {
                break;
            }
        }
        return;
    }

    if (value.isArray())
    {
        const auto array = value.toArray();
        for (const auto &child : array)
        {
            collectEmojiObjects(child, emotes, nodeCount, emojiCount,
                                shortcutCount, depth + 1);
            if (nodeCount >= MAX_JSON_NODES || emojiCount >= MAX_EMOTES ||
                shortcutCount >= MAX_SHORTCUTS)
            {
                break;
            }
        }
    }
}

ParseResult parsePage(const QByteArray &html)
{
    if (html.isEmpty() || html.size() > MAX_HTML_BYTES)
    {
        return {};
    }

    const auto initialData = extractInitialData(html);
    if (!initialData)
    {
        return {};
    }

    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(*initialData, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        return {};
    }

    auto emotes = std::make_shared<EmoteMap>(*YouTubeEmotes::globalEmotes());
    std::size_t nodeCount = 0;
    std::size_t emojiCount = 0;
    std::size_t shortcutCount = 0;
    collectEmojiObjects(document.object(), *emotes, nodeCount, emojiCount,
                        shortcutCount, 0);

    const auto config = extractYtConfig(html);
    const auto context = config.value(u"INNERTUBE_CONTEXT"_s).toObject();
    const auto client = context.value(u"client"_s).toObject();

    auto page = std::make_shared<YouTubeLiveChatPage>();
    page->innertubeContext = context;
    page->apiKey = configString(config, u"INNERTUBE_API_KEY");
    page->clientName = configString(config, u"INNERTUBE_CONTEXT_CLIENT_NAME");
    if (page->clientName.isEmpty())
    {
        page->clientName = client.value(u"clientName"_s).toString();
    }
    page->clientVersion = configString(config, u"INNERTUBE_CLIENT_VERSION");
    if (page->clientVersion.isEmpty())
    {
        page->clientVersion =
            configString(config, u"INNERTUBE_CONTEXT_CLIENT_VERSION");
    }
    if (page->clientVersion.isEmpty())
    {
        page->clientVersion = client.value(u"clientVersion"_s).toString();
    }
    page->visitorData = client.value(u"visitorData"_s).toString();
    if (page->visitorData.isEmpty())
    {
        page->visitorData = configString(config, u"VISITOR_DATA");
    }
    page->continuation =
        liveChatContinuation(document.object(), &page->clickTrackingParams);
    page->emotes = emotes->empty()
                       ? EMPTY_EMOTE_MAP
                       : std::shared_ptr<const EmoteMap>{std::move(emotes)};

    QString validationError;
    if (!validBounded(page->apiKey, 512) ||
        !validBounded(page->clientName, 64) ||
        !validBounded(page->clientVersion, 128) ||
        !validBounded(page->continuation, 64 * 1024) ||
        page->visitorData.size() > 8 * 1024 ||
        page->clickTrackingParams.size() > 16 * 1024 ||
        page->innertubeContext.isEmpty())
    {
        validationError =
            u"YouTube's live chat page omitted required client data."_s;
    }

    return {
        .page = std::move(page),
        .error = std::move(validationError),
        .recognized = true,
    };
}

void startLoad(const QString &videoID)
{
    auto &loader = loaderState();
    ++loader.inFlight;

    QUrl url(u"https://www.youtube.com/live_chat"_s);
    QUrlQuery query;
    query.addQueryItem(u"is_popout"_s, u"1"_s);
    query.addQueryItem(u"v"_s, videoID);
    query.addQueryItem(u"hl"_s, u"en"_s);
    url.setQuery(query);

    QNetworkRequest request(url);
    request.setRawHeader("Accept-Language", "en-US,en;q=0.9");
    request.setRawHeader("User-Agent", YOUTUBE_USER_AGENT);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);

    auto *reply = networkManager()->get(request);
    auto requestState = std::make_shared<RequestState>();
    requestState->data.reserve(256 * 1024);

    auto *timer = new QTimer(reply);
    timer->setSingleShot(true);
    timer->start(REQUEST_TIMEOUT_MS);

    QObject::connect(timer, &QTimer::timeout, reply, [reply, requestState] {
        if (!requestState->finished)
        {
            requestState->rejected = true;
            requestState->timedOut = true;
            reply->abort();
        }
    });
    QObject::connect(
        reply, &QNetworkReply::readyRead, reply, [reply, requestState] {
            const auto chunk = reply->readAll();
            if (requestState->rejected)
            {
                return;
            }
            if (chunk.size() > MAX_HTML_BYTES - requestState->data.size())
            {
                requestState->rejected = true;
                requestState->data.clear();
                reply->abort();
                return;
            }
            requestState->data.append(chunk);
        });
    QObject::connect(
        reply, &QNetworkReply::downloadProgress, reply,
        [reply, requestState](qint64 received, qint64 total) {
            if (!requestState->rejected &&
                (received > MAX_HTML_BYTES || total > MAX_HTML_BYTES))
            {
                requestState->rejected = true;
                requestState->data.clear();
                reply->abort();
            }
        });
    QObject::connect(
        reply, &QNetworkReply::finished, reply,
        [reply, timer, requestState, videoID] {
            requestState->finished = true;
            timer->stop();
            if (!requestState->rejected)
            {
                const auto tail = reply->readAll();
                if (tail.size() <= MAX_HTML_BYTES - requestState->data.size())
                {
                    requestState->data.append(tail);
                }
                else
                {
                    requestState->rejected = true;
                }
            }

            ParseResult parsed;
            if (!requestState->rejected &&
                reply->error() == QNetworkReply::NoError)
            {
                parsed = parsePage(requestState->data);
            }
            if (requestState->rejected)
            {
                parsed.error =
                    requestState->timedOut
                        ? u"YouTube's live chat page took too long to load."_s
                        : u"YouTube's live chat page exceeded the safety limit."_s;
            }
            else if (reply->error() != QNetworkReply::NoError)
            {
                parsed.error =
                    u"YouTube's live chat page could not be loaded."_s;
            }
            finishLoad(videoID, std::move(parsed));
            reply->deleteLater();
        });
}

}  // namespace

namespace chatterino {

EmotePtr YouTubeEmotes::makeCustomEmoji(const QString &shortcut,
                                        const QString &imageUrl,
                                        const QString &emojiID)
{
    return std::make_shared<const Emote>(Emote{
        .name = EmoteName{shortcut},
        .images = ImageSet(Image::fromAutoscaledUrl(
            Url{imageUrl}, youtube::CUSTOM_EMOJI_LOGICAL_SIZE)),
        .tooltip = Tooltip{shortcut.toHtmlEscaped() % u"<br>YouTube Emoji"_s},
        .id = EmoteId{emojiID},
    });
}

std::shared_ptr<const EmoteMap> YouTubeEmotes::globalEmotes()
{
    static const auto *emotes =
        new std::shared_ptr<const EmoteMap>(makeGlobalEmotes());
    return *emotes;
}

void YouTubeEmotes::loadPageForVideo(const QString &videoID,
                                     PageCallback callback, bool fresh)
{
    Q_ASSERT(QCoreApplication::instance() == nullptr ||
             QThread::currentThread() ==
                 QCoreApplication::instance()->thread());

    const auto normalizedID = videoID.trimmed();
    if (!isValidVideoID(normalizedID))
    {
        invokeLater(std::move(callback),
                    makeUnexpected(u"That YouTube video ID is invalid."_s));
        return;
    }

    auto &state = loaderState();
    const auto now = std::chrono::steady_clock::now();
    const auto existing = state.cache.find(normalizedID);
    if (existing != state.cache.end())
    {
        existing->second.lastUse = ++state.useCounter;
        const auto cachedState = existing->second.state;
        if (cachedState == CacheState::Failed && !fresh &&
            now < existing->second.retryAfter)
        {
            invokeLater(std::move(callback),
                        makeUnexpected(existing->second.error));
            return;
        }
        if (cachedState == CacheState::Ready && !fresh)
        {
            if (auto page = existing->second.page.lock())
            {
                invokeLater(std::move(callback), std::move(page));
                return;
            }

            existing->second.state = CacheState::Loading;
            if (state.inFlight >= MAX_IN_FLIGHT)
            {
                invokeLater(std::move(callback),
                            makeUnexpected(
                                u"Too many YouTube chat pages are loading."_s));
                existing->second.state = CacheState::Ready;
                return;
            }
            if (callback)
            {
                existing->second.callbacks.emplace_back(std::move(callback));
            }
            startLoad(normalizedID);
            return;
        }
        if (cachedState == CacheState::Loading)
        {
            if (callback)
            {
                existing->second.callbacks.emplace_back(std::move(callback));
            }
            return;
        }

        if (state.inFlight >= MAX_IN_FLIGHT)
        {
            invokeLater(
                std::move(callback),
                makeUnexpected(u"Too many YouTube chat pages are loading."_s));
            return;
        }

        existing->second.state = CacheState::Loading;
        existing->second.retryAfter = {};
        if (callback)
        {
            existing->second.callbacks.emplace_back(std::move(callback));
        }
        startLoad(normalizedID);
        return;
    }

    trimCache();
    if (state.cache.size() >= MAX_CACHE_ENTRIES ||
        state.inFlight >= MAX_IN_FLIGHT)
    {
        invokeLater(
            std::move(callback),
            makeUnexpected(u"Too many YouTube chat pages are loading."_s));
        return;
    }

    CacheEntry entry;
    entry.lastUse = ++state.useCounter;
    if (callback)
    {
        entry.callbacks.emplace_back(std::move(callback));
    }
    state.cache.emplace(normalizedID, std::move(entry));
    startLoad(normalizedID);
}

void YouTubeEmotes::loadForVideo(const QString &videoID, Callback callback)
{
    loadPageForVideo(videoID, [callback = std::move(callback)](
                                  ExpectedStr<PagePtr> result) mutable {
        if (!callback)
        {
            return;
        }
        if (!result || !*result || !(*result)->emotes)
        {
            callback(EMPTY_EMOTE_MAP);
            return;
        }
        callback((*result)->emotes);
    });
}

std::shared_ptr<const EmoteMap> YouTubeEmotes::parseLiveChatPage(
    const QByteArray &html)
{
    const auto parsed = parsePage(html);
    return parsed.page && parsed.page->emotes ? parsed.page->emotes
                                              : EMPTY_EMOTE_MAP;
}

ExpectedStr<YouTubeEmotes::PagePtr> YouTubeEmotes::parseLiveChatPageData(
    const QByteArray &html)
{
    const auto parsed = parsePage(html);
    if (!parsed.recognized || !parsed.page)
    {
        return makeUnexpected(
            parsed.error.trimmed().isEmpty()
                ? u"YouTube did not expose live chat page data."_s
                : parsed.error);
    }
    if (!parsed.error.trimmed().isEmpty())
    {
        return makeUnexpected(parsed.error);
    }
    return parsed.page;
}

}  // namespace chatterino
