#pragma once

namespace wsh::vt
{
enum class ParseState
{
    Ground,
    Escape,
    Csi
};
} // namespace wsh::vt
