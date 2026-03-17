#include "wsh/conpty/conpty_session.h"

namespace wsh::conpty
{
wsh::common::Result<void> ConptySession::Start(const std::wstring&, short, short)
{
    return {};
}

wsh::common::Result<void> ConptySession::Resize(short, short)
{
    return {};
}

wsh::common::Result<void> ConptySession::WriteInput(std::span<const std::byte>)
{
    return {};
}

wsh::common::Result<std::string> ConptySession::ReadOutputChunk()
{
    return std::string{};
}

void ConptySession::Shutdown()
{
}
} // namespace wsh::conpty
