#include "wsh/common/strings.h"

#include <algorithm>
#include <cctype>

namespace wsh::common
{
std::string Trim(std::string_view value)
{
    auto begin = value.begin();
    auto end = value.end();

    while (begin != end && std::isspace(static_cast<unsigned char>(*begin)) != 0)
    {
        ++begin;
    }

    while (begin != end && std::isspace(static_cast<unsigned char>(*(end - 1))) != 0)
    {
        --end;
    }

    return std::string(begin, end);
}
} // namespace wsh::common
