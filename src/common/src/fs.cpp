#include "wsh/common/fs.h"

namespace wsh::common
{
std::filesystem::path NormalizePath(const std::filesystem::path& path)
{
    return path.lexically_normal();
}
} // namespace wsh::common
