#pragma once

#include <cstddef>
#include <span>
#include <string>

#include "wsh/common/result.h"

namespace wsh::conpty
{
class ConptySession
{
public:
    ConptySession() = default;
    ~ConptySession() = default;

    [[nodiscard]] wsh::common::Result<void> Start(const std::wstring& commandLine, short cols, short rows);
    [[nodiscard]] wsh::common::Result<void> Resize(short cols, short rows);
    [[nodiscard]] wsh::common::Result<void> WriteInput(std::span<const std::byte> data);
    [[nodiscard]] wsh::common::Result<std::string> ReadOutputChunk();
    void Shutdown();
};
} // namespace wsh::conpty
