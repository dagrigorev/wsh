#include <iostream>
#include <string>

#include "wsh/platform/windows/execution_helpers.h"

int main()
{
    using wsh::platform::windows::RunInteractiveAsciiProbe;
    using wsh::platform::windows::RunPwshEncodedProbe;

    std::cout << "WSH Terminal Phase 2.4 cleanup/helpers bootstrap OK\n";
    std::cout << "\n=== Interactive ASCII probe ===\n";

    const auto ascii = RunInteractiveAsciiProbe(
        L"pwsh.exe -NoLogo -NoProfile",
        "Write-Output 'WSH_CONPTY_OK'\r\nexit\r\n");

    if (!ascii.HasValue())
    {
        std::cout << "ASCII probe failed\n";
        return 1;
    }

    std::cout << ascii.Value().snapshotUtf8 << "\n";

    std::cout << "\n=== Encoded Unicode probe ===\n";
    const std::wstring script =
        L"Write-Output 'WSH_CONPTY_OK'; "
        L"Write-Output 'Привет_Кириллица';";

    const auto unicode = RunPwshEncodedProbe(L"pwsh.exe", script);
    if (!unicode.HasValue())
    {
        std::cout << "Encoded Unicode probe failed\n";
        return 1;
    }

    std::cout << unicode.Value().snapshotUtf8 << "\n";
    std::cout << "\nStatus: interactive ASCII path kept as MVP; EncodedCommand path kept as reliable Unicode path.\n";
    return 0;
}
