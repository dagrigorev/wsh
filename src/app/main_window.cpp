#include "app/main_window.h"

#include "core/logger.h"
#include "core/utf.h"
#include "platform/clipboard.h"
#include "terminal/input_translator.h"

#include <algorithm>
#include <cwctype>
#include <format>
#include <string>
#include <unordered_map>
#include <windowsx.h>
#include <dwmapi.h>

using Microsoft::WRL::ComPtr;

namespace wsh::app
{
    namespace
    {
        constexpr wchar_t kWindowClassName[] = L"WSH.Terminal.MainWindow";
        constexpr UINT kProfileMenuBase = 40000;
        constexpr int kResizeBorder = 8;

        enum WindowControlId
        {
            kControlMinimize = 0,
            kControlMaximize = 1,
            kControlClose = 2
        };

        enum ShellToolbarButtonId
        {
            kShellPreviousSession = 0,
            kShellDuplicateSession = 1,
            kShellNewWorkspace = 2
        };

        enum ShellTrafficDotId
        {
            kShellCloseSession = 0,
            kShellMinimizeWindow = 1,
            kShellMaximizeWindow = 2
        };

        enum SidebarButtonId
        {
            kSidebarNewSession = 0,
            kSidebarWorkspaceMenu = 1,
            kSidebarFocusSearch = 2,
            kSidebarReloadSettings = 3
        };

        bool IsCtrlPressed() noexcept { return (::GetKeyState(VK_CONTROL) & 0x8000) != 0; }
        bool IsShiftPressed() noexcept { return (::GetKeyState(VK_SHIFT) & 0x8000) != 0; }
        bool IsAltPressed() noexcept { return (::GetKeyState(VK_MENU) & 0x8000) != 0; }

        D2D1_RECT_F MakeRect(float left, float top, float right, float bottom)
        {
            return D2D1::RectF(left, top, right, bottom);
        }
    }

    bool MainWindow::Create(HINSTANCE instance, int showCommand)
    {
        instance_ = instance;
        settings_ = config::LoadSettings(SettingsPath());
        workspace_.emplace(settings_);

        WNDCLASSEXW windowClass{};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
        windowClass.lpfnWndProc = &MainWindow::WindowProc;
        windowClass.hInstance = instance;
        windowClass.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
        windowClass.hbrBackground = nullptr;
        windowClass.lpszClassName = kWindowClassName;

        ::RegisterClassExW(&windowClass);

        hwnd_ = ::CreateWindowExW(
            WS_EX_APPWINDOW,
            kWindowClassName,
            L"WSH Terminal",
            WS_POPUP | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_VISIBLE,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            1280,
            820,
            nullptr,
            nullptr,
            instance,
            this);

        if (hwnd_ == nullptr)
        {
            return false;
        }

        const auto cornerPref = DWM_WINDOW_CORNER_PREFERENCE::DWMWCP_ROUND;
        ::DwmSetWindowAttribute(hwnd_, DWMWA_WINDOW_CORNER_PREFERENCE, &cornerPref, sizeof(cornerPref));
        const BOOL darkMode = TRUE;
        ::DwmSetWindowAttribute(hwnd_, DWMWA_USE_IMMERSIVE_DARK_MODE, &darkMode, sizeof(darkMode));

        ::ShowWindow(hwnd_, showCommand);
        ::UpdateWindow(hwnd_);
        return true;
    }

    std::wstring MainWindow::SettingsPath() const
    {
        return L"assets\\wsh_profiles.toml";
    }

    LRESULT CALLBACK MainWindow::WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
        MainWindow* window = nullptr;
        if (message == WM_NCCREATE)
        {
            const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
            window = static_cast<MainWindow*>(create->lpCreateParams);
            ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(window));
            window->hwnd_ = hwnd;
        }
        else
        {
            window = reinterpret_cast<MainWindow*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        }

        return window ? window->HandleMessage(message, wParam, lParam) : ::DefWindowProcW(hwnd, message, wParam, lParam);
    }

    LRESULT MainWindow::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam)
    {
        switch (message)
        {
        case WM_NCCALCSIZE:
            return 0;
        case WM_NCHITTEST:
        {
            POINT point{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            RECT windowRect{};
            ::GetWindowRect(hwnd_, &windowRect);

            const bool onLeft = point.x >= windowRect.left && point.x < windowRect.left + kResizeBorder;
            const bool onRight = point.x < windowRect.right && point.x >= windowRect.right - kResizeBorder;
            const bool onTop = point.y >= windowRect.top && point.y < windowRect.top + kResizeBorder;
            const bool onBottom = point.y < windowRect.bottom && point.y >= windowRect.bottom - kResizeBorder;

            if (onTop && onLeft) return HTTOPLEFT;
            if (onTop && onRight) return HTTOPRIGHT;
            if (onBottom && onLeft) return HTBOTTOMLEFT;
            if (onBottom && onRight) return HTBOTTOMRIGHT;
            if (onLeft) return HTLEFT;
            if (onRight) return HTRIGHT;
            if (onTop) return HTTOP;
            if (onBottom) return HTBOTTOM;

            POINT clientPoint = point;
            ::ScreenToClient(hwnd_, &clientPoint);
            if (HitTestWindowControl(clientPoint.x, clientPoint.y).has_value())
            {
                return HTCLIENT;
            }
            if (IsPointInDraggableHeader(clientPoint.x, clientPoint.y))
            {
                return HTCAPTION;
            }
            return HTCLIENT;
        }
        case WM_CREATE:
            OnCreate();
            return 0;
        case WM_SIZE:
            OnSize();
            return 0;
        case WM_PAINT:
            OnPaint();
            return 0;
        case WM_CHAR:
            OnChar(static_cast<wchar_t>(wParam));
            return 0;
        case WM_KEYDOWN:
            OnKeyDown(wParam, lParam);
            return 0;
        case WM_MOUSEWHEEL:
            OnMouseWheel(GET_WHEEL_DELTA_WPARAM(wParam));
            return 0;
        case WM_LBUTTONDOWN:
            OnLeftButtonDown(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            return 0;
        case WM_MOUSEMOVE:
            OnMouseMove(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), wParam);
            return 0;
        case WM_LBUTTONDBLCLK:
            OnLeftButtonDoubleClick(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            return 0;
        case WM_MOUSELEAVE:
            OnMouseLeave();
            return 0;
        case WM_SETCURSOR:
            if (LOWORD(lParam) == HTCLIENT)
            {
                POINT point{};
                ::GetCursorPos(&point);
                ::ScreenToClient(hwnd_, &point);
                UpdateHoverState(point.x, point.y);
                UpdateCursor();
                return TRUE;
            }
            break;
        case WM_LBUTTONUP:
            OnLeftButtonUp(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            return 0;
        case WM_MBUTTONDOWN:
            OnMiddleButtonDown(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            return 0;
        case WM_MBUTTONUP:
            OnMiddleButtonUp(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            return 0;
        case WM_TIMER:
            Invalidate();
            return 0;
        case WM_DESTROY:
            ::PostQuitMessage(0);
            return 0;
        default:
            return ::DefWindowProcW(hwnd_, message, wParam, lParam);
        }
    }

    void MainWindow::OnCreate()
    {
        EnsureFactories();
        RecomputeMetrics();
        ::SetTimer(hwnd_, 1, 33, nullptr);
        ::SetFocus(hwnd_);
        OpenProfile(0);
        UpdateWindowTitle();
        UpdateCursor();
    }

    void MainWindow::EnsureFactories()
    {
        if (!d2dFactory_)
        {
            ::D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2dFactory_.ReleaseAndGetAddressOf());
        }

        if (!dwriteFactory_)
        {
            ::DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(dwriteFactory_.ReleaseAndGetAddressOf()));
        }

        if (!terminalFormat_)
        {
            dwriteFactory_->CreateTextFormat(
                settings_.fontFamily.c_str(),
                nullptr,
                DWRITE_FONT_WEIGHT_REGULAR,
                DWRITE_FONT_STYLE_NORMAL,
                DWRITE_FONT_STRETCH_NORMAL,
                settings_.fontSize,
                L"ru-RU",
                terminalFormat_.ReleaseAndGetAddressOf());
            terminalFormat_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        }

        if (!uiFormat_)
        {
            dwriteFactory_->CreateTextFormat(
                L"Segoe UI",
                nullptr,
                DWRITE_FONT_WEIGHT_SEMI_BOLD,
                DWRITE_FONT_STYLE_NORMAL,
                DWRITE_FONT_STRETCH_NORMAL,
                14.0f,
                L"ru-RU",
                uiFormat_.ReleaseAndGetAddressOf());
            uiFormat_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        }
    }

    void MainWindow::EnsureRenderTarget()
    {
        if (renderTarget_)
        {
            return;
        }

        RECT rect{};
        ::GetClientRect(hwnd_, &rect);

        d2dFactory_->CreateHwndRenderTarget(
            D2D1::RenderTargetProperties(),
            D2D1::HwndRenderTargetProperties(hwnd_, D2D1::SizeU(rect.right - rect.left, rect.bottom - rect.top)),
            renderTarget_.ReleaseAndGetAddressOf());
    }

    void MainWindow::RecomputeMetrics()
    {
        EnsureFactories();
        ComPtr<IDWriteTextLayout> layout;
        const wchar_t* probe = L"W";
        dwriteFactory_->CreateTextLayout(probe, 1, terminalFormat_.Get(), 100.0f, 100.0f, layout.GetAddressOf());
        DWRITE_TEXT_METRICS metrics{};
        layout->GetMetrics(&metrics);
        charWidth_ = metrics.widthIncludingTrailingWhitespace;
        lineHeight_ = metrics.height + 2.0f;
    }

        void MainWindow::ResizeTerminalToClient()
    {
        RECT rect{};
        ::GetClientRect(hwnd_, &rect);

        constexpr int sidebarWidth = 268;
        constexpr int terminalWrapPadding = 14;
        constexpr int terminalShellHeader = 42;
        constexpr int terminalFrameMargin = 12;
        constexpr int terminalPaddingX = 26;
        constexpr int terminalPaddingY = 24;

        const int clientWidth = rect.right - rect.left;
        const int clientHeight = rect.bottom - rect.top;
        const int contentWidth = std::max(320, clientWidth - sidebarWidth);
        const int contentHeight = std::max(240, clientHeight - appHeaderHeight_ - tabBarHeight_ - statusBarHeight_);

        const int width = std::max(100, contentWidth - terminalWrapPadding * 2 - terminalFrameMargin * 2 - terminalPaddingX * 2 - 4);
        const int height = std::max(100, contentHeight - terminalWrapPadding * 2 - terminalShellHeader - terminalFrameMargin * 2 - terminalPaddingY * 2 - 4);
        terminalColumns_ = std::max(20, static_cast<int>(width / charWidth_));
        terminalRows_ = std::max(8, static_cast<int>(height / lineHeight_));

        if (workspace_)
        {
            for (const auto& tab : workspace_->Tabs())
            {
                tab->Resize(terminalColumns_, terminalRows_);
            }
        }
    }

    void MainWindow::OnSize()
    {
        if (renderTarget_)
        {
            RECT rect{};
            ::GetClientRect(hwnd_, &rect);
            renderTarget_->Resize(D2D1::SizeU(rect.right - rect.left, rect.bottom - rect.top));
        }

        ResizeTerminalToClient();
        Invalidate();
    }

    void MainWindow::Invalidate()
    {
        ::InvalidateRect(hwnd_, nullptr, FALSE);
    }

    void MainWindow::FocusActiveTerminal(const bool followBottom)
    {
        selectionStart_.reset();
        selectionEnd_.reset();
        if (auto* tab = workspace_ ? workspace_->ActiveTab() : nullptr)
        {
            if (followBottom)
            {
                std::scoped_lock lock(tab->Mutex());
                tab->Buffer().FollowBottom();
            }
        }
        ::SetFocus(hwnd_);
    }

    void MainWindow::OnPaint()
    {
        PAINTSTRUCT paint{};
        ::BeginPaint(hwnd_, &paint);

        EnsureRenderTarget();
        renderTarget_->BeginDraw();
        renderTarget_->Clear(settings_.theme.background);

        DrawHeader();
        DrawWindowControls();
        DrawTabs();
        DrawTerminal();
        DrawStatusBar();

        if (FAILED(renderTarget_->EndDraw()))
        {
            renderTarget_.Reset();
        }
        ::EndPaint(hwnd_, &paint);
        UpdateWindowTitle();
    }

    std::wstring MainWindow::BuildTabLabel(const size_t index) const
    {
        if (!workspace_ || index >= workspace_->Tabs().size())
        {
            return L"";
        }

        std::unordered_map<std::wstring, int> seen;
        for (size_t i = 0; i <= index; ++i)
        {
            const auto& name = workspace_->Tabs()[i]->ProfileName();
            ++seen[name];
        }

        const std::wstring& name = workspace_->Tabs()[index]->ProfileName();
        const int count = seen[name];
        if (count <= 1)
        {
            return name;
        }

        return std::format(L"{} #{}", name, count);
    }

        std::wstring MainWindow::Ellipsize(const std::wstring& text, const size_t maxChars) const
    {
        if (text.size() <= maxChars)
        {
            return text;
        }
        if (maxChars <= 4)
        {
            return text.substr(0, maxChars);
        }

        const auto slashPos = text.find_last_of(L"/\\");
        if (slashPos != std::wstring::npos)
        {
            const std::wstring tail = text.substr(slashPos + 1);
            if (tail.size() + 4 <= maxChars)
            {
                const size_t headCount = maxChars - tail.size() - 2;
                const size_t prefixStart = (slashPos > headCount) ? (slashPos - headCount) : 0;
                const std::wstring head = text.substr(prefixStart, std::min(slashPos, headCount));
                return L"…" + head + L"\\" + tail;
            }
            if (tail.size() + 2 <= maxChars)
            {
                return L"…\\" + tail.substr(tail.size() - (maxChars - 2));
            }
        }

        return text.substr(0, maxChars - 1) + L"…";
    }

        void MainWindow::DrawHeader()
    {
        RECT rect{};
        ::GetClientRect(hwnd_, &rect);

        constexpr float sidebarWidth = 268.0f;
        constexpr float titlePadLeft = 18.0f;
        constexpr float titlePadRight = 16.0f;
        constexpr float logoSize = 34.0f;
        constexpr float iconButton = 40.0f;

        ComPtr<ID2D1SolidColorBrush> brush;
        renderTarget_->CreateSolidColorBrush(settings_.theme.foreground, brush.GetAddressOf());

        D2D1_GRADIENT_STOP stops[] = {
            {0.0f, D2D1::ColorF(0.06f, 0.07f, 0.09f, 1.0f)},
            {0.45f, D2D1::ColorF(0.06f, 0.07f, 0.10f, 1.0f)},
            {1.0f, D2D1::ColorF(0.04f, 0.05f, 0.07f, 1.0f)}
        };
        ComPtr<ID2D1GradientStopCollection> stopCollection;
        renderTarget_->CreateGradientStopCollection(stops, ARRAYSIZE(stops), stopCollection.GetAddressOf());
        ComPtr<ID2D1LinearGradientBrush> gradient;
        renderTarget_->CreateLinearGradientBrush(
            D2D1::LinearGradientBrushProperties(
                D2D1::Point2F(0.0f, 0.0f),
                D2D1::Point2F(0.0f, static_cast<float>(rect.bottom))),
            stopCollection.Get(),
            gradient.GetAddressOf());
        renderTarget_->FillRectangle(MakeRect(0.0f, 0.0f, static_cast<float>(rect.right), static_cast<float>(rect.bottom)), gradient.Get());


        const D2D1_RECT_F titlebar = MakeRect(0.0f, 0.0f, static_cast<float>(rect.right), static_cast<float>(appHeaderHeight_));
        const D2D1_RECT_F sidebar = MakeRect(0.0f, static_cast<float>(appHeaderHeight_), sidebarWidth, static_cast<float>(rect.bottom));
        const D2D1_RECT_F content = MakeRect(sidebarWidth, static_cast<float>(appHeaderHeight_), static_cast<float>(rect.right), static_cast<float>(rect.bottom));

        brush->SetColor(D2D1::ColorF(1, 1, 1, 0.03f));
        renderTarget_->FillRectangle(titlebar, brush.Get());
        brush->SetColor(D2D1::ColorF(1, 1, 1, 0.05f));
        renderTarget_->DrawLine(D2D1::Point2F(0.0f, static_cast<float>(appHeaderHeight_) - 0.5f), D2D1::Point2F(static_cast<float>(rect.right), static_cast<float>(appHeaderHeight_) - 0.5f), brush.Get(), 1.0f);

        brush->SetColor(D2D1::ColorF(1, 1, 1, 0.025f));
        renderTarget_->FillRectangle(sidebar, brush.Get());
        brush->SetColor(D2D1::ColorF(1, 1, 1, 0.08f));
        renderTarget_->DrawLine(D2D1::Point2F(sidebarWidth - 0.5f, static_cast<float>(appHeaderHeight_)), D2D1::Point2F(sidebarWidth - 0.5f, static_cast<float>(rect.bottom)), brush.Get(), 1.0f);

        brush->SetColor(D2D1::ColorF(1, 1, 1, 0.015f));
        renderTarget_->FillRectangle(content, brush.Get());

        const D2D1_RECT_F logoRect = MakeRect(titlePadLeft, 15.0f, titlePadLeft + logoSize, 15.0f + logoSize);
        brush->SetColor(D2D1::ColorF(0.35f, 0.78f, 1.0f, 1.0f));
        renderTarget_->FillRoundedRectangle(D2D1::RoundedRect(logoRect, 10.0f, 10.0f), brush.Get());
        brush->SetColor(D2D1::ColorF(0.48f, 0.30f, 1.0f, 0.92f));
        renderTarget_->FillRoundedRectangle(D2D1::RoundedRect(MakeRect(titlePadLeft + 12.0f, 23.0f, titlePadLeft + logoSize, 49.0f), 10.0f, 10.0f), brush.Get());
        brush->SetColor(D2D1::ColorF(1, 1, 1, 0.16f));
        renderTarget_->DrawRoundedRectangle(D2D1::RoundedRect(logoRect, 10.0f, 10.0f), brush.Get(), 1.0f);

        ComPtr<IDWriteTextFormat> appNameFormat;
        dwriteFactory_->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 20.0f, L"ru-RU", appNameFormat.GetAddressOf());
        appNameFormat->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        brush->SetColor(D2D1::ColorF(0.93f, 0.95f, 0.98f, 1.0f));
        renderTarget_->DrawTextW(L"WSH", 3, appNameFormat.Get(), MakeRect(64.0f, 16.0f, 130.0f, 50.0f), brush.Get());

        const float rightControlsWidth = iconButton + 3.0f * 42.0f + 12.0f;
        const float centerLeft = 150.0f;
        const float centerRight = static_cast<float>(rect.right) - rightControlsWidth - titlePadRight - 10.0f;
        const float pillWidth = std::min(760.0f, std::max(360.0f, centerRight - centerLeft - 40.0f));
        const float pillLeft = centerLeft + (centerRight - centerLeft - pillWidth) * 0.5f;
        const D2D1_RECT_F pillRect = MakeRect(pillLeft, 13.0f, pillLeft + pillWidth, 51.0f);
        brush->SetColor(D2D1::ColorF(1, 1, 1, 0.04f));
        renderTarget_->FillRoundedRectangle(D2D1::RoundedRect(pillRect, 19.0f, 19.0f), brush.Get());
        brush->SetColor(D2D1::ColorF(1, 1, 1, 0.07f));
        renderTarget_->DrawRoundedRectangle(D2D1::RoundedRect(pillRect, 19.0f, 19.0f), brush.Get(), 1.0f);

        ComPtr<IDWriteTextFormat> pillStrongFormat;
        dwriteFactory_->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 14.0f, L"ru-RU", pillStrongFormat.GetAddressOf());
        pillStrongFormat->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        ComPtr<IDWriteTextFormat> pillFormat;
        dwriteFactory_->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 14.0f, L"ru-RU", pillFormat.GetAddressOf());
        pillFormat->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);

        brush->SetColor(D2D1::ColorF(0.93f, 0.95f, 0.98f, 1.0f));
        const std::wstring workspaceName = workspace_ ? workspace_->ActiveWorkspaceName() : L"Workspace";
        const std::wstring workspaceLabel = Ellipsize(workspaceName, 14);
        renderTarget_->DrawTextW(workspaceLabel.c_str(), static_cast<UINT32>(workspaceLabel.size()), pillStrongFormat.Get(), MakeRect(pillRect.left + 16.0f, pillRect.top + 8.0f, pillRect.left + 128.0f, pillRect.bottom), brush.Get());
        brush->SetColor(D2D1::ColorF(0.66f, 0.70f, 0.78f, 1.0f));
        renderTarget_->DrawTextW(L"•", 1, pillFormat.Get(), MakeRect(pillRect.left + 118.0f, pillRect.top + 8.0f, pillRect.left + 130.0f, pillRect.bottom), brush.Get());
        const std::wstring mid = L"~/projects/wsh";
        std::wstring rightText = L"PowerShell";
        if (workspace_ && workspace_->ActiveTab())
        {
            const auto name = workspace_->ActiveTab()->ProfileName();
            if (!name.empty())
            {
                rightText = Ellipsize(name, 14);
            }
        }
        brush->SetColor(D2D1::ColorF(0.77f, 0.80f, 0.87f, 1.0f));
        const std::wstring midLabel = Ellipsize(mid, 18);
        renderTarget_->DrawTextW(midLabel.c_str(), static_cast<UINT32>(midLabel.size()), pillFormat.Get(), MakeRect(pillRect.left + 138.0f, pillRect.top + 8.0f, pillRect.left + 318.0f, pillRect.bottom), brush.Get());
        brush->SetColor(D2D1::ColorF(0.66f, 0.70f, 0.78f, 1.0f));
        renderTarget_->DrawTextW(L"•", 1, pillFormat.Get(), MakeRect(pillRect.left + 324.0f, pillRect.top + 8.0f, pillRect.left + 336.0f, pillRect.bottom), brush.Get());
        brush->SetColor(D2D1::ColorF(0.77f, 0.80f, 0.87f, 1.0f));
        renderTarget_->DrawTextW(rightText.c_str(), static_cast<UINT32>(rightText.size()), pillFormat.Get(), MakeRect(pillRect.left + 342.0f, pillRect.top + 8.0f, pillRect.right - 16.0f, pillRect.bottom), brush.Get());

        const float iconLeft = static_cast<float>(rect.right) - rightControlsWidth - 8.0f;
        const D2D1_RECT_F iconRect = MakeRect(iconLeft, 12.0f, iconLeft + iconButton, 52.0f);
        brush->SetColor(D2D1::ColorF(1, 1, 1, 0.04f));
        renderTarget_->FillRoundedRectangle(D2D1::RoundedRect(iconRect, 12.0f, 12.0f), brush.Get());
        brush->SetColor(D2D1::ColorF(1, 1, 1, 0.07f));
        renderTarget_->DrawRoundedRectangle(D2D1::RoundedRect(iconRect, 12.0f, 12.0f), brush.Get(), 1.0f);
        brush->SetColor(D2D1::ColorF(0.90f, 0.92f, 0.96f, 0.95f));
        const float ix = iconRect.left + 12.0f;
        const float iy = iconRect.top + 12.0f;
        renderTarget_->DrawLine(D2D1::Point2F(ix, iy), D2D1::Point2F(ix + 15.0f, iy), brush.Get(), 1.6f);
        renderTarget_->DrawLine(D2D1::Point2F(ix, iy + 7.0f), D2D1::Point2F(ix + 11.0f, iy + 7.0f), brush.Get(), 1.6f);
        renderTarget_->DrawLine(D2D1::Point2F(ix, iy + 14.0f), D2D1::Point2F(ix + 14.0f, iy + 14.0f), brush.Get(), 1.6f);
    }

        void MainWindow::DrawWindowControls()
    {
        RECT rect{};
        ::GetClientRect(hwnd_, &rect);

        ComPtr<ID2D1SolidColorBrush> brush;
        renderTarget_->CreateSolidColorBrush(settings_.theme.foreground, brush.GetAddressOf());

        const float buttonSize = 42.0f;
        const float gap = 4.0f;
        const float top = 13.0f;
        float left = static_cast<float>(rect.right) - (buttonSize * 3.0f + gap * 2.0f) - 16.0f;

        for (int i = 0; i < 3; ++i)
        {
            const bool hovered = hoveredWindowControl_ && *hoveredWindowControl_ == i;
            const bool pressed = pressedWindowControl_ && *pressedWindowControl_ == i;
            const D2D1_RECT_F r = MakeRect(left, top, left + buttonSize, top + 38.0f);

            D2D1_COLOR_F fill = D2D1::ColorF(1, 1, 1, hovered ? 0.08f : 0.0f);
            if (pressed) fill = D2D1::ColorF(1, 1, 1, 0.12f);
            if (i == kControlClose && hovered) fill = D2D1::ColorF(1.0f, 0.42f, 0.50f, pressed ? 0.28f : 0.16f);
            brush->SetColor(fill);
            renderTarget_->FillRoundedRectangle(D2D1::RoundedRect(r, 10.0f, 10.0f), brush.Get());

            brush->SetColor(D2D1::ColorF(0.92f, 0.95f, 0.98f, 0.96f));
            const float cx = (r.left + r.right) * 0.5f;
            const float cy = (r.top + r.bottom) * 0.5f;
            if (i == kControlMinimize)
            {
                renderTarget_->DrawLine(D2D1::Point2F(cx - 7.0f, cy + 5.0f), D2D1::Point2F(cx + 7.0f, cy + 5.0f), brush.Get(), 1.7f);
            }
            else if (i == kControlMaximize)
            {
                renderTarget_->DrawRoundedRectangle(D2D1::RoundedRect(MakeRect(cx - 7.0f, cy - 7.0f, cx + 7.0f, cy + 7.0f), 2.0f, 2.0f), brush.Get(), 1.55f);
            }
            else
            {
                renderTarget_->DrawLine(D2D1::Point2F(cx - 6.0f, cy - 6.0f), D2D1::Point2F(cx + 6.0f, cy + 6.0f), brush.Get(), 1.7f);
                renderTarget_->DrawLine(D2D1::Point2F(cx + 6.0f, cy - 6.0f), D2D1::Point2F(cx - 6.0f, cy + 6.0f), brush.Get(), 1.7f);
            }
            left += buttonSize + gap;
        }
    }

        void MainWindow::DrawTabs()
    {
        if (!workspace_)
        {
            return;
        }

        RECT rect{};
        ::GetClientRect(hwnd_, &rect);
        constexpr float sidebarWidth = 268.0f;
        const float stripTop = static_cast<float>(appHeaderHeight_);
        const float stripBottom = stripTop + static_cast<float>(tabBarHeight_);

        ComPtr<ID2D1SolidColorBrush> brush;
        renderTarget_->CreateSolidColorBrush(settings_.theme.muted, brush.GetAddressOf());
        brush->SetColor(D2D1::ColorF(1, 1, 1, 0.018f));
        renderTarget_->FillRectangle(MakeRect(sidebarWidth, stripTop, static_cast<float>(rect.right), stripBottom), brush.Get());
        brush->SetColor(D2D1::ColorF(1, 1, 1, 0.05f));
        renderTarget_->DrawLine(D2D1::Point2F(sidebarWidth, stripBottom - 0.5f), D2D1::Point2F(static_cast<float>(rect.right), stripBottom - 0.5f), brush.Get(), 1.0f);

        const auto& tabs = workspace_->Tabs();
        float x = sidebarWidth + 12.0f;
        const float top = stripTop + 10.0f;
        const float height = 38.0f;
        const float width = 178.0f;

        for (size_t i = 0; i < tabs.size(); ++i)
        {
            const bool active = i == workspace_->ActiveIndex();
            const bool hovered = hoveredTab_ && *hoveredTab_ == i;
            const bool closeHovered = hoveredCloseTab_ && *hoveredCloseTab_ == i;
            const D2D1_RECT_F tabRect = MakeRect(x, top, x + width, top + height);
            brush->SetColor(active ? D2D1::ColorF(0.44f, 0.26f, 0.82f, 0.22f) : D2D1::ColorF(1, 1, 1, hovered ? 0.065f : 0.035f));
            renderTarget_->FillRoundedRectangle(D2D1::RoundedRect(tabRect, 13.0f, 13.0f), brush.Get());
            brush->SetColor(active ? D2D1::ColorF(0.55f, 0.36f, 0.96f, 0.40f) : D2D1::ColorF(1, 1, 1, 0.05f));
            renderTarget_->DrawRoundedRectangle(D2D1::RoundedRect(tabRect, 13.0f, 13.0f), brush.Get(), 1.0f);
            if (active)
            {
                brush->SetColor(D2D1::ColorF(0.55f, 0.36f, 0.96f, 0.92f));
                renderTarget_->FillRectangle(MakeRect(x + 12.0f, top + height - 3.0f, x + width - 12.0f, top + height - 1.0f), brush.Get());
            }
            const D2D1_RECT_F iconRect = MakeRect(x + 10.0f, top + 11.0f, x + 25.0f, top + 26.0f);
            brush->SetColor(D2D1::ColorF(0.35f, 0.77f, 1.0f, 1.0f));
            renderTarget_->FillRoundedRectangle(D2D1::RoundedRect(iconRect, 5.0f, 5.0f), brush.Get());
            brush->SetColor(D2D1::ColorF(0.31f, 0.55f, 1.0f, 0.95f));
            renderTarget_->FillRoundedRectangle(D2D1::RoundedRect(MakeRect(iconRect.left + 6.0f, iconRect.top + 5.0f, iconRect.right, iconRect.bottom), 5.0f, 5.0f), brush.Get());
            brush->SetColor(active ? D2D1::ColorF(0.93f, 0.95f, 0.98f, 1.0f) : D2D1::ColorF(0.66f, 0.71f, 0.79f, 1.0f));
            const std::wstring tabTitle = Ellipsize(BuildTabLabel(i), 16);
            renderTarget_->DrawTextW(tabTitle.c_str(), static_cast<UINT32>(tabTitle.size()), uiFormat_.Get(), MakeRect(x + 33.0f, top + 8.0f, x + width - 30.0f, top + height), brush.Get());
            const float closeSize = 18.0f;
            const float closeLeft = x + width - 26.0f;
            const float closeTop = top + 10.0f;
            brush->SetColor(closeHovered ? D2D1::ColorF(1, 1, 1, 0.10f) : D2D1::ColorF(1, 1, 1, 0.0f));
            renderTarget_->FillRoundedRectangle(D2D1::RoundedRect(MakeRect(closeLeft, closeTop, closeLeft + closeSize, closeTop + closeSize), 9.0f, 9.0f), brush.Get());
            brush->SetColor(D2D1::ColorF(0.79f, 0.83f, 0.90f, closeHovered ? 1.0f : 0.75f));
            const float cx = closeLeft + closeSize * 0.5f;
            const float cy = closeTop + closeSize * 0.5f;
            renderTarget_->DrawLine(D2D1::Point2F(cx - 3.5f, cy - 3.5f), D2D1::Point2F(cx + 3.5f, cy + 3.5f), brush.Get(), 1.35f);
            renderTarget_->DrawLine(D2D1::Point2F(cx + 3.5f, cy - 3.5f), D2D1::Point2F(cx - 3.5f, cy + 3.5f), brush.Get(), 1.35f);
            x += width + 8.0f;
        }
        const D2D1_RECT_F addRect = MakeRect(x, top, x + 38.0f, top + height);
        brush->SetColor(D2D1::ColorF(1, 1, 1, hoverNewTabButton_ ? 0.065f : 0.035f));
        renderTarget_->FillRoundedRectangle(D2D1::RoundedRect(addRect, 13.0f, 13.0f), brush.Get());
        brush->SetColor(D2D1::ColorF(1, 1, 1, 0.05f));
        renderTarget_->DrawRoundedRectangle(D2D1::RoundedRect(addRect, 13.0f, 13.0f), brush.Get(), 1.0f);
        brush->SetColor(D2D1::ColorF(0.90f, 0.93f, 0.97f, 0.95f));
        const float plusCx = x + 19.0f;
        const float plusCy = top + 19.0f;
        renderTarget_->DrawLine(D2D1::Point2F(plusCx - 5.0f, plusCy), D2D1::Point2F(plusCx + 5.0f, plusCy), brush.Get(), 1.7f);
        renderTarget_->DrawLine(D2D1::Point2F(plusCx, plusCy - 5.0f), D2D1::Point2F(plusCx, plusCy + 5.0f), brush.Get(), 1.7f);
        const float actionsRight = static_cast<float>(rect.right) - 12.0f;
        float actionLeft = actionsRight - 3.0f * 40.0f - 8.0f;
        for (int i = 0; i < 3; ++i)
        {
            const bool hovered = hoveredShellToolbarButton_ && *hoveredShellToolbarButton_ == i;
            const D2D1_RECT_F a = MakeRect(actionLeft, top - 1.0f, actionLeft + 40.0f, top + 39.0f);
            brush->SetColor(D2D1::ColorF(1, 1, 1, hovered ? 0.065f : 0.03f));
            renderTarget_->FillRoundedRectangle(D2D1::RoundedRect(a, 12.0f, 12.0f), brush.Get());
            brush->SetColor(D2D1::ColorF(1, 1, 1, hovered ? 0.12f : 0.07f));
            renderTarget_->DrawRoundedRectangle(D2D1::RoundedRect(a, 12.0f, 12.0f), brush.Get(), 1.0f);
            brush->SetColor(D2D1::ColorF(0.88f, 0.91f, 0.97f, 0.92f));
            const float cx = (a.left + a.right) * 0.5f;
            const float cy = (a.top + a.bottom) * 0.5f;
            if (i == 0)
            {
                renderTarget_->DrawLine(D2D1::Point2F(cx - 7.0f, cy), D2D1::Point2F(cx + 7.0f, cy), brush.Get(), 1.6f);
                renderTarget_->DrawLine(D2D1::Point2F(cx - 7.0f, cy - 6.0f), D2D1::Point2F(cx + 7.0f, cy - 6.0f), brush.Get(), 1.6f);
                renderTarget_->DrawLine(D2D1::Point2F(cx - 7.0f, cy + 6.0f), D2D1::Point2F(cx + 3.0f, cy + 6.0f), brush.Get(), 1.6f);
            }
            else if (i == 1)
            {
                renderTarget_->DrawRectangle(MakeRect(cx - 7.0f, cy - 7.0f, cx + 7.0f, cy + 7.0f), brush.Get(), 1.4f);
                renderTarget_->DrawLine(D2D1::Point2F(cx, cy - 7.0f), D2D1::Point2F(cx, cy + 7.0f), brush.Get(), 1.2f);
            }
            else
            {
                renderTarget_->DrawLine(D2D1::Point2F(cx - 7.0f, cy), D2D1::Point2F(cx + 7.0f, cy), brush.Get(), 1.6f);
                renderTarget_->DrawLine(D2D1::Point2F(cx, cy - 7.0f), D2D1::Point2F(cx, cy + 7.0f), brush.Get(), 1.6f);
            }
            actionLeft += 44.0f;
        }
    }

        void MainWindow::DrawTerminal()
    {
        auto* tab = workspace_ ? workspace_->ActiveTab() : nullptr;
        if (tab == nullptr)
        {
            return;
        }

        RECT rect{};
        ::GetClientRect(hwnd_, &rect);

        constexpr float sidebarWidth = 268.0f;
        constexpr float terminalWrapPadding = 14.0f;
        constexpr float shellHeaderHeight = 42.0f;
        constexpr float shellInnerMargin = 12.0f;
        constexpr float termPadX = 26.0f;
        constexpr float termPadY = 24.0f;

        const float contentLeft = sidebarWidth;
        const float contentTop = static_cast<float>(appHeaderHeight_ + tabBarHeight_);
        const float contentBottom = static_cast<float>(rect.bottom - statusBarHeight_);

        ComPtr<ID2D1SolidColorBrush> brush;
        renderTarget_->CreateSolidColorBrush(settings_.theme.foreground, brush.GetAddressOf());

        constexpr float sidebarButtonSize = 40.0f;
        constexpr float sidebarButtonGap = 10.0f;
        const float sidebarButtonsTop = static_cast<float>(appHeaderHeight_) + 14.0f;
        const float sidebarButtonsLeft = 14.0f;
        for (int i = 0; i < 4; ++i)
        {
            const float left = sidebarButtonsLeft + static_cast<float>(i) * (sidebarButtonSize + sidebarButtonGap);
            const D2D1_RECT_F buttonRect = MakeRect(left, sidebarButtonsTop, left + sidebarButtonSize, sidebarButtonsTop + sidebarButtonSize);
            const bool hovered = hoveredSidebarButton_ && *hoveredSidebarButton_ == i;
            brush->SetColor(i == kSidebarNewSession ? D2D1::ColorF(0.44f, 0.26f, 0.82f, hovered ? 0.34f : 0.24f)
                                                     : D2D1::ColorF(1, 1, 1, hovered ? 0.065f : 0.032f));
            renderTarget_->FillRoundedRectangle(D2D1::RoundedRect(buttonRect, 13.0f, 13.0f), brush.Get());
            brush->SetColor(D2D1::ColorF(1, 1, 1, hovered ? 0.11f : 0.06f));
            renderTarget_->DrawRoundedRectangle(D2D1::RoundedRect(buttonRect, 13.0f, 13.0f), brush.Get(), 1.0f);
            brush->SetColor(D2D1::ColorF(0.90f, 0.93f, 0.97f, 0.95f));
            const float cx = (buttonRect.left + buttonRect.right) * 0.5f;
            const float cy = (buttonRect.top + buttonRect.bottom) * 0.5f;
            switch (i)
            {
            case kSidebarNewSession:
                renderTarget_->DrawLine(D2D1::Point2F(cx - 6.0f, cy), D2D1::Point2F(cx + 6.0f, cy), brush.Get(), 1.8f);
                renderTarget_->DrawLine(D2D1::Point2F(cx, cy - 6.0f), D2D1::Point2F(cx, cy + 6.0f), brush.Get(), 1.8f);
                break;
            case kSidebarWorkspaceMenu:
                renderTarget_->DrawRectangle(MakeRect(cx - 6.0f, cy - 6.0f, cx + 6.0f, cy + 6.0f), brush.Get(), 1.5f);
                renderTarget_->DrawLine(D2D1::Point2F(cx, cy - 6.0f), D2D1::Point2F(cx, cy + 6.0f), brush.Get(), 1.3f);
                renderTarget_->DrawLine(D2D1::Point2F(cx - 6.0f, cy), D2D1::Point2F(cx + 6.0f, cy), brush.Get(), 1.3f);
                break;
            case kSidebarFocusSearch:
                renderTarget_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx - 2.0f, cy - 1.0f), 5.5f, 5.5f), brush.Get(), 1.5f);
                renderTarget_->DrawLine(D2D1::Point2F(cx + 2.5f, cy + 3.5f), D2D1::Point2F(cx + 8.0f, cy + 9.0f), brush.Get(), 1.5f);
                break;
            case kSidebarReloadSettings:
                renderTarget_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), 6.0f, 6.0f), brush.Get(), 1.4f);
                renderTarget_->DrawLine(D2D1::Point2F(cx, cy - 9.0f), D2D1::Point2F(cx, cy - 6.0f), brush.Get(), 1.4f);
                renderTarget_->DrawLine(D2D1::Point2F(cx, cy + 6.0f), D2D1::Point2F(cx, cy + 9.0f), brush.Get(), 1.4f);
                renderTarget_->DrawLine(D2D1::Point2F(cx - 9.0f, cy), D2D1::Point2F(cx - 6.0f, cy), brush.Get(), 1.4f);
                renderTarget_->DrawLine(D2D1::Point2F(cx + 6.0f, cy), D2D1::Point2F(cx + 9.0f, cy), brush.Get(), 1.4f);
                break;
            }
        }

        const D2D1_RECT_F searchRect = MakeRect(14.0f, appHeaderHeight_ + 68.0f, sidebarWidth - 14.0f, appHeaderHeight_ + 112.0f);
        brush->SetColor(D2D1::ColorF(1, 1, 1, hoverSearchBox_ || searchFocused_ ? 0.055f : 0.035f));
        renderTarget_->FillRoundedRectangle(D2D1::RoundedRect(searchRect, 14.0f, 14.0f), brush.Get());
        brush->SetColor(D2D1::ColorF(1, 1, 1, searchFocused_ ? 0.12f : 0.06f));
        renderTarget_->DrawRoundedRectangle(D2D1::RoundedRect(searchRect, 14.0f, 14.0f), brush.Get(), 1.0f);
        brush->SetColor(searchQuery_.empty() ? D2D1::ColorF(0.67f, 0.71f, 0.79f, 0.96f) : D2D1::ColorF(0.88f, 0.92f, 0.98f, 0.98f));
        const std::wstring searchLabel = searchQuery_.empty() ? L"Search sessions" : Ellipsize(searchQuery_, 20);
        const D2D1_RECT_F searchTextRect = MakeRect(52.0f, appHeaderHeight_ + 81.0f, sidebarWidth - 20.0f, appHeaderHeight_ + 106.0f);
        renderTarget_->DrawTextW(searchLabel.c_str(), static_cast<UINT32>(searchLabel.size()), uiFormat_.Get(), searchTextRect, brush.Get());
        if (searchFocused_)
        {
            const bool blinkOn = ((::GetTickCount64() / 530ULL) % 2ULL) == 0ULL;
            if (blinkOn)
            {
                float caretX = searchTextRect.left;
                if (!searchQuery_.empty())
                {
                    ComPtr<IDWriteTextLayout> searchLayout;
                    const std::wstring visibleSearch = Ellipsize(searchQuery_, 20);
                    if (SUCCEEDED(dwriteFactory_->CreateTextLayout(visibleSearch.c_str(), static_cast<UINT32>(visibleSearch.size()), uiFormat_.Get(), 400.0f, 32.0f, searchLayout.GetAddressOf())))
                    {
                        DWRITE_TEXT_METRICS metrics{};
                        if (SUCCEEDED(searchLayout->GetMetrics(&metrics)))
                        {
                            caretX += metrics.widthIncludingTrailingWhitespace + 1.0f;
                        }
                    }
                }
                caretX = std::min(caretX, searchTextRect.right - 2.0f);
                brush->SetColor(D2D1::ColorF(0.94f, 0.96f, 0.99f, 0.96f));
                renderTarget_->FillRectangle(MakeRect(caretX, searchTextRect.top - 1.0f, caretX + 1.5f, searchTextRect.bottom - 2.0f), brush.Get());
                brush->SetColor(searchQuery_.empty() ? D2D1::ColorF(0.67f, 0.71f, 0.79f, 0.96f) : D2D1::ColorF(0.88f, 0.92f, 0.98f, 0.98f));
            }
        }
        const float sx = 28.0f;
        const float sy = static_cast<float>(appHeaderHeight_) + 80.0f;
        renderTarget_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(sx + 7.0f, sy + 7.0f), 6.5f, 6.5f), brush.Get(), 1.4f);
        renderTarget_->DrawLine(D2D1::Point2F(sx + 12.0f, sy + 12.0f), D2D1::Point2F(sx + 18.0f, sy + 18.0f), brush.Get(), 1.4f);

        ComPtr<IDWriteTextFormat> smallCaps;
        dwriteFactory_->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 12.0f, L"ru-RU", smallCaps.GetAddressOf());
        smallCaps->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        brush->SetColor(D2D1::ColorF(0.49f, 0.54f, 0.64f, 1.0f));
        renderTarget_->DrawTextW(L"SESSIONS", 8, smallCaps.Get(), MakeRect(20.0f, appHeaderHeight_ + 130.0f, sidebarWidth - 20.0f, appHeaderHeight_ + 152.0f), brush.Get());

        auto visibleSessions = workspace_ ? workspace_->FilteredSessionIndices(searchQuery_) : std::vector<size_t>{};
        const D2D1_COLOR_F badgeColors[] = {
            D2D1::ColorF(0.49f, 0.91f, 0.53f, 1.0f),
            D2D1::ColorF(0.31f, 0.55f, 1.0f, 1.0f),
            D2D1::ColorF(0.96f, 0.76f, 0.47f, 1.0f),
            D2D1::ColorF(0.20f, 0.83f, 0.60f, 1.0f)
        };
        const D2D1_COLOR_F iconColors[] = {
            D2D1::ColorF(0.35f, 0.77f, 1.0f, 1.0f),
            D2D1::ColorF(0.47f, 0.51f, 0.56f, 1.0f),
            D2D1::ColorF(0.39f, 0.45f, 0.54f, 1.0f),
            D2D1::ColorF(0.20f, 0.83f, 0.60f, 1.0f)
        };

        const float sessionTopStart = static_cast<float>(appHeaderHeight_) + 160.0f;
        const float sessionStep = 66.0f;
        const float sessionHeight = 58.0f;
        const size_t shown = std::min<size_t>(5, visibleSessions.size());
        for (size_t visualIndex = 0; visualIndex < shown; ++visualIndex)
        {
            const size_t sessionIndex = visibleSessions[visualIndex];
            const bool active = workspace_ && sessionIndex == workspace_->ActiveIndex();
            const bool hovered = hoveredSidebarSession_ && *hoveredSidebarSession_ == sessionIndex;
            const float itemTop = sessionTopStart + static_cast<float>(visualIndex) * sessionStep;
            const D2D1_RECT_F itemRect = MakeRect(14.0f, itemTop, sidebarWidth - 14.0f, itemTop + sessionHeight);
            brush->SetColor(active ? D2D1::ColorF(0.55f, 0.36f, 0.96f, 0.18f) : D2D1::ColorF(1, 1, 1, hovered ? 0.045f : 0.0f));
            renderTarget_->FillRoundedRectangle(D2D1::RoundedRect(itemRect, 16.0f, 16.0f), brush.Get());
            brush->SetColor(active ? D2D1::ColorF(0.55f, 0.36f, 0.96f, 0.26f) : D2D1::ColorF(1, 1, 1, hovered ? 0.08f : 0.05f));
            renderTarget_->DrawRoundedRectangle(D2D1::RoundedRect(itemRect, 16.0f, 16.0f), brush.Get(), 1.0f);
            const D2D1_RECT_F iconRect = MakeRect(26.0f, itemTop + 12.0f, 60.0f, itemTop + 46.0f);
            brush->SetColor(iconColors[sessionIndex % 4]);
            renderTarget_->FillRoundedRectangle(D2D1::RoundedRect(iconRect, 10.0f, 10.0f), brush.Get());
            brush->SetColor(D2D1::ColorF(1, 1, 1, 0.95f));
            const auto* sessionTab = workspace_->Tabs()[sessionIndex].get();
            const wchar_t letter = sessionTab->ProfileName().empty() ? L'?' : static_cast<wchar_t>(std::towupper(sessionTab->ProfileName()[0]));
            const wchar_t one[2] = { letter, 0 };
            renderTarget_->DrawTextW(one, 1, uiFormat_.Get(), MakeRect(37.0f, itemTop + 17.0f, 50.0f, itemTop + 38.0f), brush.Get());
            brush->SetColor(D2D1::ColorF(0.93f, 0.95f, 0.98f, 1.0f));
            const std::wstring name = Ellipsize(BuildTabLabel(sessionIndex), 16);
            renderTarget_->DrawTextW(name.c_str(), static_cast<UINT32>(name.size()), uiFormat_.Get(), MakeRect(72.0f, itemTop + 10.0f, sidebarWidth - 42.0f, itemTop + 30.0f), brush.Get());
            std::wstring subtitle = sessionTab->TitleSnapshot();
            if (subtitle.empty() || subtitle == sessionTab->ProfileName())
            {
                subtitle = sessionIndex == 0 ? L"C:\projects\wsh" : (sessionIndex == 1 ? L"~/src/wsh" : (sessionIndex == 2 ? L"docker compose up" : L"active shell"));
            }
            subtitle = Ellipsize(subtitle, 22);
            brush->SetColor(D2D1::ColorF(0.49f, 0.54f, 0.64f, 0.95f));
            renderTarget_->DrawTextW(subtitle.c_str(), static_cast<UINT32>(subtitle.size()), uiFormat_.Get(), MakeRect(72.0f, itemTop + 28.0f, sidebarWidth - 42.0f, itemTop + 48.0f), brush.Get());
            brush->SetColor(badgeColors[sessionIndex % 4]);
            renderTarget_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(sidebarWidth - 30.0f, itemTop + 29.0f), 5.0f, 5.0f), brush.Get());
        }

        brush->SetColor(D2D1::ColorF(0.49f, 0.54f, 0.64f, 1.0f));
        renderTarget_->DrawTextW(L"WORKSPACE", 9, smallCaps.Get(), MakeRect(20.0f, appHeaderHeight_ + 506.0f, sidebarWidth - 20.0f, appHeaderHeight_ + 526.0f), brush.Get());
        const D2D1_RECT_F queueRect = MakeRect(14.0f, appHeaderHeight_ + 538.0f, sidebarWidth - 14.0f, appHeaderHeight_ + 596.0f);
        brush->SetColor(D2D1::ColorF(1, 1, 1, 0.03f));
        renderTarget_->FillRoundedRectangle(D2D1::RoundedRect(queueRect, 16.0f, 16.0f), brush.Get());
        brush->SetColor(D2D1::ColorF(1, 1, 1, 0.05f));
        renderTarget_->DrawRoundedRectangle(D2D1::RoundedRect(queueRect, 16.0f, 16.0f), brush.Get(), 1.0f);
        brush->SetColor(D2D1::ColorF(0.93f, 0.95f, 0.98f, 1.0f));
        const std::wstring queueTitle = workspace_ ? Ellipsize(workspace_->ActiveWorkspaceName(), 16) : L"Build queue";
        renderTarget_->DrawTextW(queueTitle.c_str(), static_cast<UINT32>(queueTitle.size()), uiFormat_.Get(), MakeRect(26.0f, appHeaderHeight_ + 552.0f, 200.0f, appHeaderHeight_ + 570.0f), brush.Get());
        brush->SetColor(D2D1::ColorF(0.49f, 0.54f, 0.64f, 0.96f));
        const std::wstring queueSub = std::format(L"{} session(s)", workspace_ ? workspace_->Tabs().size() : 0);
        renderTarget_->DrawTextW(queueSub.c_str(), static_cast<UINT32>(queueSub.size()), uiFormat_.Get(), MakeRect(26.0f, appHeaderHeight_ + 570.0f, 180.0f, appHeaderHeight_ + 588.0f), brush.Get());
        brush->SetColor(D2D1::ColorF(0.96f, 0.76f, 0.47f, 1.0f));
        renderTarget_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(sidebarWidth - 30.0f, appHeaderHeight_ + 567.0f), 5.0f, 5.0f), brush.Get());

        const D2D1_RECT_F footerRect = MakeRect(14.0f, static_cast<float>(rect.bottom) - 132.0f, sidebarWidth - 14.0f, static_cast<float>(rect.bottom) - 18.0f);
        brush->SetColor(D2D1::ColorF(1, 1, 1, 0.035f));
        renderTarget_->FillRoundedRectangle(D2D1::RoundedRect(footerRect, 18.0f, 18.0f), brush.Get());
        brush->SetColor(D2D1::ColorF(1, 1, 1, 0.06f));
        renderTarget_->DrawRoundedRectangle(D2D1::RoundedRect(footerRect, 18.0f, 18.0f), brush.Get(), 1.0f);
        brush->SetColor(D2D1::ColorF(0.93f, 0.95f, 0.98f, 1.0f));
        renderTarget_->DrawTextW(L"WSH Workspace", 13, uiFormat_.Get(), MakeRect(28.0f, footerRect.top + 16.0f, footerRect.right - 18.0f, footerRect.top + 36.0f), brush.Get());
        brush->SetColor(D2D1::ColorF(0.67f, 0.71f, 0.79f, 0.95f));
        const std::wstring footerText = workspace_ ? std::format(L"{} workspace(s) • {} session(s)", workspace_->Workspaces().size(), workspace_->Tabs().size()) : L"Material styled terminal shell";
        renderTarget_->DrawTextW(footerText.c_str(), static_cast<UINT32>(footerText.size()), uiFormat_.Get(), MakeRect(28.0f, footerRect.top + 42.0f, footerRect.right - 18.0f, footerRect.bottom - 14.0f), brush.Get());

        const float shellLeft = contentLeft + terminalWrapPadding;
        const float shellTop = contentTop + terminalWrapPadding;
        const float shellRight = static_cast<float>(rect.right) - terminalWrapPadding;
        const float shellBottom = contentBottom - terminalWrapPadding;
        const D2D1_RECT_F shellRect = MakeRect(shellLeft, shellTop, shellRight, shellBottom);
        brush->SetColor(D2D1::ColorF(0.01f, 0.02f, 0.04f, 0.28f));
        renderTarget_->FillRoundedRectangle(D2D1::RoundedRect(MakeRect(shellLeft, shellTop + 8.0f, shellRight, shellBottom + 10.0f), 22.0f, 22.0f), brush.Get());
        brush->SetColor(D2D1::ColorF(1, 1, 1, 0.025f));
        renderTarget_->FillRoundedRectangle(D2D1::RoundedRect(shellRect, 22.0f, 22.0f), brush.Get());
        brush->SetColor(D2D1::ColorF(1, 1, 1, 0.07f));
        renderTarget_->DrawRoundedRectangle(D2D1::RoundedRect(shellRect, 22.0f, 22.0f), brush.Get(), 1.0f);

        const D2D1_RECT_F topline = MakeRect(shellLeft, shellTop, shellRight, shellTop + shellHeaderHeight);
        brush->SetColor(D2D1::ColorF(1, 1, 1, 0.02f));
        renderTarget_->FillRoundedRectangle(D2D1::RoundedRect(topline, 22.0f, 22.0f), brush.Get());
        brush->SetColor(D2D1::ColorF(1, 1, 1, 0.05f));
        renderTarget_->DrawLine(D2D1::Point2F(shellLeft, shellTop + shellHeaderHeight - 0.5f), D2D1::Point2F(shellRight, shellTop + shellHeaderHeight - 0.5f), brush.Get(), 1.0f);
        const D2D1_COLOR_F trafficColors[] = {D2D1::ColorF(1.0f, 0.42f, 0.50f, 1.0f), D2D1::ColorF(0.96f, 0.76f, 0.47f, 1.0f), D2D1::ColorF(0.49f, 0.91f, 0.53f, 1.0f)};
        for (int i = 0; i < 3; ++i)
        {
            brush->SetColor(trafficColors[i]);
            renderTarget_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(shellLeft + 22.0f + i * 18.0f, shellTop + 21.0f), 5.0f, 5.0f), brush.Get());
        }
        ComPtr<IDWriteTextFormat> metaFormat;
        dwriteFactory_->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 13.0f, L"ru-RU", metaFormat.GetAddressOf());
        metaFormat->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        brush->SetColor(D2D1::ColorF(0.66f, 0.71f, 0.79f, 0.98f));
        std::wstring termTitle = workspace_ && workspace_->ActiveTab() ? std::format(L"{} • ACTIVE SESSION", workspace_->ActiveTab()->ProfileName()) : L"LOCAL SHELL • ACTIVE SESSION";
        termTitle = Ellipsize(termTitle, 28);
        renderTarget_->DrawTextW(termTitle.c_str(), static_cast<UINT32>(termTitle.size()), metaFormat.Get(), MakeRect(shellLeft + 86.0f, shellTop + 13.0f, shellRight - 24.0f, shellTop + 34.0f), brush.Get());

        const float termOuterLeft = shellLeft + shellInnerMargin;
        const float termOuterTop = shellTop + shellHeaderHeight + shellInnerMargin;
        const float termOuterRight = shellRight - shellInnerMargin;
        const float termOuterBottom = shellBottom - shellInnerMargin;
        brush->SetColor(D2D1::ColorF(0.04f, 0.06f, 0.09f, 1.0f));
        renderTarget_->FillRoundedRectangle(D2D1::RoundedRect(MakeRect(termOuterLeft, termOuterTop, termOuterRight, termOuterBottom), 18.0f, 18.0f), brush.Get());
        brush->SetColor(D2D1::ColorF(1, 1, 1, 0.05f));
        renderTarget_->DrawRoundedRectangle(D2D1::RoundedRect(MakeRect(termOuterLeft, termOuterTop, termOuterRight, termOuterBottom), 18.0f, 18.0f), brush.Get(), 1.0f);

        const float left = termOuterLeft + termPadX;
        const float top = termOuterTop + termPadY;
        const float clipRight = left + terminalColumns_ * charWidth_;
        const float clipBottom = top + terminalRows_ * lineHeight_;
        renderTarget_->PushAxisAlignedClip(MakeRect(left, top, clipRight, clipBottom), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        const auto selectionLeft = selectionStart_ && selectionEnd_ ? std::min(*selectionStart_, *selectionEnd_) : wsh::terminal::SelectionPoint{};
        const auto selectionRight = selectionStart_ && selectionEnd_ ? std::max(*selectionStart_, *selectionEnd_) : wsh::terminal::SelectionPoint{};
        const bool hasSelection = selectionStart_.has_value() && selectionEnd_.has_value();
        std::scoped_lock lock(tab->Mutex());
        const auto& lines = tab->Buffer().Lines();
        const int viewportTop = tab->Buffer().ViewportTop();
        for (int row = 0; row < terminalRows_; ++row)
        {
            const int bufferRow = viewportTop + row;
            if (bufferRow >= static_cast<int>(lines.size())) break;
            const float y = top + row * lineHeight_;
            for (int column = 0; column < terminalColumns_ && column < static_cast<int>(lines[bufferRow].size()); ++column)
            {
                const auto& cell = lines[bufferRow][column];
                const float x = left + column * charWidth_;
                const bool selected = hasSelection && wsh::terminal::SelectionPoint{ bufferRow, column } >= selectionLeft && wsh::terminal::SelectionPoint{ bufferRow, column } <= selectionRight;
                D2D1_COLOR_F background = cell.background;
                D2D1_COLOR_F foreground = cell.foreground;
                if (cell.inverse) std::swap(background, foreground);
                if (background.a > 0.01f)
                {
                    brush->SetColor(background);
                    renderTarget_->FillRectangle(MakeRect(x, y, x + charWidth_, y + lineHeight_), brush.Get());
                }
                if (selected)
                {
                    brush->SetColor(D2D1::ColorF(0.55f, 0.36f, 0.96f, 0.36f));
                    renderTarget_->FillRectangle(MakeRect(x, y, x + charWidth_, y + lineHeight_), brush.Get());
                    foreground = D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f);
                }
                brush->SetColor(foreground);
                const wchar_t glyph[2] = { cell.glyph == L'\000' ? L' ' : cell.glyph, (wchar_t)0 };
                renderTarget_->DrawTextW(glyph, 1, terminalFormat_.Get(), MakeRect(x, y, x + charWidth_ * 2.0f, y + lineHeight_), brush.Get());
                if (cell.underline)
                {
                    renderTarget_->DrawLine(D2D1::Point2F(x, y + lineHeight_ - 2.0f), D2D1::Point2F(x + charWidth_, y + lineHeight_ - 2.0f), brush.Get(), 1.0f);
                }
            }
        }
        const auto& cursor = tab->Buffer().GetCursor();
        const bool blinkOn = ((::GetTickCount64() / 530ULL) % 2ULL) == 0ULL;
        if (cursor.visible && blinkOn)
        {
            brush->SetColor(D2D1::ColorF(0.96f, 0.97f, 1.0f, 1.0f));
            const int relativeRow = cursor.row - viewportTop;
            if (relativeRow >= 0 && relativeRow < terminalRows_)
            {
                const float x = left + cursor.column * charWidth_;
                const float y = top + relativeRow * lineHeight_;
                if (settings_.cursorStyle == config::CursorStyle::Underline)
                {
                    renderTarget_->FillRectangle(MakeRect(x, y + lineHeight_ - 3.0f, x + charWidth_, y + lineHeight_ - 1.0f), brush.Get());
                }
                else if (settings_.cursorStyle == config::CursorStyle::Block)
                {
                    renderTarget_->FillRectangle(MakeRect(x, y, x + charWidth_ - 1.0f, y + lineHeight_ - 1.0f), brush.Get());
                }
                else
                {
                    renderTarget_->FillRectangle(MakeRect(x, y, x + 2.0f, y + lineHeight_ - 2.0f), brush.Get());
                }
            }
        }
        renderTarget_->PopAxisAlignedClip();
    }

        void MainWindow::DrawStatusBar()
    {
        RECT rect{};
        ::GetClientRect(hwnd_, &rect);
        constexpr float sidebarWidth = 268.0f;
        const float left = sidebarWidth;
        const float top = static_cast<float>(rect.bottom - statusBarHeight_);
        const float right = static_cast<float>(rect.right);
        const float bottom = static_cast<float>(rect.bottom);

        ComPtr<ID2D1SolidColorBrush> brush;
        renderTarget_->CreateSolidColorBrush(settings_.theme.foreground, brush.GetAddressOf());
        brush->SetColor(D2D1::ColorF(1, 1, 1, 0.018f));
        renderTarget_->FillRectangle(MakeRect(left, top, right, bottom), brush.Get());
        brush->SetColor(D2D1::ColorF(1, 1, 1, 0.05f));
        renderTarget_->DrawLine(D2D1::Point2F(left, top + 0.5f), D2D1::Point2F(right, top + 0.5f), brush.Get(), 1.0f);

        auto drawDivider = [&](float x)
        {
            brush->SetColor(D2D1::ColorF(1, 1, 1, 0.08f));
            renderTarget_->DrawLine(D2D1::Point2F(x, top + 13.0f), D2D1::Point2F(x, bottom - 13.0f), brush.Get(), 1.0f);
        };

        float x = left + 16.0f;
        brush->SetColor(D2D1::ColorF(0.49f, 0.91f, 0.53f, 1.0f));
        renderTarget_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(x + 4.0f, top + 20.0f), 4.0f, 4.0f), brush.Get());
        x += 16.0f;
        const std::wstring profile = workspace_ && workspace_->ActiveTab() ? workspace_->ActiveTab()->ProfileName() : L"pwsh";
        brush->SetColor(D2D1::ColorF(0.78f, 0.82f, 0.89f, 0.98f));
        renderTarget_->DrawTextW(profile.c_str(), static_cast<UINT32>(profile.size()), uiFormat_.Get(), MakeRect(x, top + 10.0f, x + 120.0f, bottom), brush.Get());
        x += 110.0f; drawDivider(x); x += 12.0f;
        renderTarget_->DrawTextW(L"UTF-8", 5, uiFormat_.Get(), MakeRect(x, top + 10.0f, x + 60.0f, bottom), brush.Get());
        x += 64.0f; drawDivider(x); x += 12.0f;
        renderTarget_->DrawTextW(L"LF", 2, uiFormat_.Get(), MakeRect(x, top + 10.0f, x + 28.0f, bottom), brush.Get());
        x += 34.0f; drawDivider(x); x += 12.0f;
        renderTarget_->DrawTextW(L"x64", 3, uiFormat_.Get(), MakeRect(x, top + 10.0f, x + 40.0f, bottom), brush.Get());
        x += 50.0f; drawDivider(x); x += 12.0f;
        renderTarget_->DrawTextW(L"Ready", 5, uiFormat_.Get(), MakeRect(x, top + 10.0f, x + 60.0f, bottom), brush.Get());
        std::wstring active = workspace_ && workspace_->ActiveTab() ? workspace_->ActiveTab()->TitleSnapshot() : L"";
        if (active.empty()) active = L"main";
        active = Ellipsize(active, 34);
        brush->SetColor(D2D1::ColorF(0.49f, 0.54f, 0.64f, 0.96f));
        renderTarget_->DrawTextW(active.c_str(), static_cast<UINT32>(active.size()), uiFormat_.Get(), MakeRect(right - 240.0f, top + 10.0f, right - 16.0f, bottom), brush.Get());
    }

    void MainWindow::OnChar(const wchar_t ch)
    {
        if (searchFocused_)
        {
            if (ch >= 0x20 && ch != 0x7F)
            {
                searchQuery_.push_back(ch);
                Invalidate();
            }
            return;
        }

        auto* tab = workspace_ ? workspace_->ActiveTab() : nullptr;
        if (tab == nullptr)
        {
            return;
        }

        if (IsCtrlPressed() || IsAltPressed())
        {
            return;
        }

        if (ch < 0x20 || ch == 0x7F)
        {
            return;
        }

        tab->SendInput(core::WideToUtf8(std::wstring(1, ch)));
        Invalidate();
    }

void MainWindow::ShowProfileMenu(const int x, const int y)
    {
        if (!workspace_)
        {
            return;
        }

        HMENU menu = ::CreatePopupMenu();
        if (menu == nullptr)
        {
            return;
        }

        for (size_t i = 0; i < settings_.profiles.size(); ++i)
        {
            ::AppendMenuW(menu, MF_STRING, kProfileMenuBase + static_cast<UINT>(i), settings_.profiles[i].name.c_str());
        }

        POINT screenPoint{ x, y };
        ::ClientToScreen(hwnd_, &screenPoint);
        const UINT command = ::TrackPopupMenu(menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN, screenPoint.x, screenPoint.y, 0, hwnd_, nullptr);
        ::DestroyMenu(menu);

        if (command >= kProfileMenuBase)
        {
            OpenProfile(command - kProfileMenuBase);
        }
    }
void MainWindow::ShowWorkspaceMenu(const int x, const int y)
    {
        if (!workspace_)
        {
            return;
        }

        HMENU menu = ::CreatePopupMenu();
        if (menu == nullptr)
        {
            return;
        }

        constexpr UINT kWorkspaceMenuBase = 41000;
        constexpr UINT kWorkspaceNewId = 41999;
        const auto& groups = workspace_->Workspaces();
        for (size_t i = 0; i < groups.size(); ++i)
        {
            UINT flags = MF_STRING;
            if (i == workspace_->ActiveWorkspaceIndex())
            {
                flags |= MF_CHECKED;
            }
            ::AppendMenuW(menu, flags, kWorkspaceMenuBase + static_cast<UINT>(i), groups[i].name.c_str());
        }
        if (!groups.empty())
        {
            ::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        }
        ::AppendMenuW(menu, MF_STRING, kWorkspaceNewId, L"New workspace");

        POINT screenPoint{ x, y };
        ::ClientToScreen(hwnd_, &screenPoint);
        const UINT command = ::TrackPopupMenu(menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN, screenPoint.x, screenPoint.y, 0, hwnd_, nullptr);
        ::DestroyMenu(menu);

        if (command >= kWorkspaceMenuBase && command < kWorkspaceMenuBase + groups.size())
        {
            workspace_->ActivateWorkspace(command - kWorkspaceMenuBase);
            if (workspace_->Tabs().empty())
            {
                OpenProfile(0);
            }
            UpdateWindowTitle();
            Invalidate();
            return;
        }

        if (command == kWorkspaceNewId)
        {
            const size_t newIndex = workspace_->AddWorkspace();
            workspace_->ActivateWorkspace(newIndex);
            OpenProfile(0);
            UpdateWindowTitle();
            Invalidate();
        }
    }


void MainWindow::OnKeyDown(const WPARAM key, LPARAM)
    {
        if (searchFocused_)
        {
            if (key == VK_ESCAPE)
            {
                searchFocused_ = false;
                Invalidate();
                return;
            }
            if (key == VK_BACK)
            {
                if (!searchQuery_.empty())
                {
                    searchQuery_.pop_back();
                    Invalidate();
                }
                return;
            }
            if (key == VK_RETURN)
            {
                searchFocused_ = false;
                Invalidate();
                return;
            }
        }

        auto* tab = workspace_ ? workspace_->ActiveTab() : nullptr;
        if (!workspace_ || tab == nullptr)
        {
            return;
        }

        if (IsCtrlPressed() && IsShiftPressed() && key == 'T')
        {
            ShowProfileMenu(padding_ + 24, appHeaderHeight_ + tabBarHeight_);
            return;
        }
        if (IsCtrlPressed() && key == 'T')
        {
            OpenProfile(0);
            return;
        }
        if (IsCtrlPressed() && key == 'W')
        {
            CloseActiveTab();
            return;
        }
        if (IsCtrlPressed() && !IsShiftPressed() && key == VK_TAB)
        {
            workspace_->NextTab();
            UpdateWindowTitle();
            Invalidate();
            return;
        }
        if (IsCtrlPressed() && IsShiftPressed() && key == VK_TAB)
        {
            workspace_->PreviousTab();
            UpdateWindowTitle();
            Invalidate();
            return;
        }
        if (IsCtrlPressed() && !IsShiftPressed() && key == 'C')
        {
            tab->SendInput("\x03");
            return;
        }
        if (IsCtrlPressed() && IsShiftPressed() && key == 'C')
        {
            CopySelection();
            return;
        }
        if ((IsCtrlPressed() && IsShiftPressed() && key == 'V') || (IsShiftPressed() && key == VK_INSERT))
        {
            PasteClipboard();
            return;
        }
        if (key == VK_F1) { OpenProfile(0); return; }
        if (key == VK_F2) { OpenProfile(1); return; }
        if (key == VK_F3) { OpenProfile(2); return; }

        switch (key)
        {
        case VK_HOME: tab->SendInput("\x1b[H"); break;
        case VK_END: tab->SendInput("\x1b[F"); break;
        case VK_DELETE: tab->SendInput("\x1b[3~"); break;
        case VK_ESCAPE: tab->SendInput("\x1b"); break;
        case VK_PRIOR: tab->Scroll(-terminalRows_ / 2); Invalidate(); return;
        case VK_NEXT: tab->Scroll(terminalRows_ / 2); Invalidate(); return;
        case VK_RETURN: tab->SendInput("\r"); break;
        case VK_BACK: tab->SendInput("\b"); break;
        case VK_TAB: tab->SendInput(IsShiftPressed() ? "\x1b[Z" : "\t"); break;
        case VK_LEFT: tab->SendInput("\x1b[D"); break;
        case VK_RIGHT: tab->SendInput("\x1b[C"); break;
        case VK_UP: tab->SendInput("\x1b[A"); break;
        case VK_DOWN: tab->SendInput("\x1b[B"); break;
        default: return;
        }

        Invalidate();
    }

void MainWindow::OnMouseWheel(const short delta)
    {
        if (auto* tab = workspace_ ? workspace_->ActiveTab() : nullptr)
        {
            const int step = std::max(3, terminalRows_ / 8);
            tab->Scroll(delta > 0 ? -step : step);
            if (selecting_)
            {
                POINT point{};
                ::GetCursorPos(&point);
                ::ScreenToClient(hwnd_, &point);
                selectionEnd_ = ClientToBufferPoint(point.x, point.y);
            }
            Invalidate();
        }
    }

        bool MainWindow::IsPointInTerminal(const int x, const int y) const
    {
        constexpr int sidebarWidth = 268;
        constexpr int terminalWrapPadding = 14;
        constexpr int shellHeaderHeight = 42;
        constexpr int shellInnerMargin = 12;
        constexpr int termPadX = 26;
        constexpr int termPadY = 24;
        const int left = sidebarWidth + terminalWrapPadding + shellInnerMargin + termPadX;
        const int top = appHeaderHeight_ + tabBarHeight_ + terminalWrapPadding + shellHeaderHeight + shellInnerMargin + termPadY;
        const int right = left + static_cast<int>(terminalColumns_ * charWidth_);
        const int bottom = top + static_cast<int>(terminalRows_ * lineHeight_);
        return x >= left && x <= right && y >= top && y <= bottom;
    }

        bool MainWindow::IsPointInDraggableHeader(const int x, const int y) const
    {
        if (y < 0 || y > appHeaderHeight_)
        {
            return false;
        }
        if (HitTestWindowControl(x, y).has_value())
        {
            return false;
        }
        if (x >= 420 && x <= 980)
        {
            return false;
        }
        if (x >= 1060)
        {
            return false;
        }
        return true;
    }

        std::optional<int> MainWindow::HitTestWindowControl(const int x, const int y) const
    {
        RECT rect{};
        ::GetClientRect(hwnd_, &rect);
        const float buttonSize = 42.0f;
        const float gap = 4.0f;
        const float top = 13.0f;
        float left = static_cast<float>(rect.right) - (buttonSize * 3.0f + gap * 2.0f) - 16.0f;
        for (int i = 0; i < 3; ++i)
        {
            if (x >= left && x <= left + buttonSize && y >= top && y <= top + 38.0f)
            {
                return i;
            }
            left += buttonSize + gap;
        }
        return std::nullopt;
    }

        wsh::terminal::SelectionPoint MainWindow::ClientToBufferPoint(const int x, const int y) const
    {
        auto* tab = workspace_ ? workspace_->ActiveTab() : nullptr;
        if (tab == nullptr)
        {
            return {};
        }
        constexpr int sidebarWidth = 268;
        constexpr int terminalWrapPadding = 14;
        constexpr int shellHeaderHeight = 42;
        constexpr int shellInnerMargin = 12;
        constexpr int termPadX = 26;
        constexpr int termPadY = 24;
        const int column = std::clamp(static_cast<int>((x - sidebarWidth - terminalWrapPadding - shellInnerMargin - termPadX) / charWidth_), 0, terminalColumns_ - 1);
        const int row = std::clamp(static_cast<int>((y - appHeaderHeight_ - tabBarHeight_ - terminalWrapPadding - shellHeaderHeight - shellInnerMargin - termPadY) / lineHeight_), 0, terminalRows_ - 1);
        return { tab->Buffer().ViewportTop() + row, column };
    }

        std::optional<size_t> MainWindow::HitTestTab(const int x, const int y) const
    {
        if (!workspace_ || y < appHeaderHeight_ || y > appHeaderHeight_ + tabBarHeight_)
        {
            return std::nullopt;
        }
        float left = 268.0f + 12.0f;
        const float top = static_cast<float>(appHeaderHeight_ + 10);
        const float height = 38.0f;
        const float width = 178.0f;
        for (size_t i = 0; i < workspace_->Tabs().size(); ++i)
        {
            if (x >= left && x <= left + width && y >= top && y <= top + height)
            {
                return i;
            }
            left += width + 8.0f;
        }
        return std::nullopt;
    }

        std::optional<size_t> MainWindow::HitTestTabClose(const int x, const int y) const
    {
        if (!workspace_ || y < appHeaderHeight_ || y > appHeaderHeight_ + tabBarHeight_)
        {
            return std::nullopt;
        }
        float left = 268.0f + 12.0f;
        const float top = static_cast<float>(appHeaderHeight_ + 10);
        const float width = 178.0f;
        for (size_t i = 0; i < workspace_->Tabs().size(); ++i)
        {
            const float closeLeft = left + width - 26.0f;
            const float closeTop = top + 10.0f;
            if (x >= closeLeft && x <= closeLeft + 18.0f && y >= closeTop && y <= closeTop + 18.0f)
            {
                return i;
            }
            left += width + 8.0f;
        }
        return std::nullopt;
    }

        bool MainWindow::IsPointInNewTabButton(const int x, const int y) const
    {
        if (!workspace_ || y < appHeaderHeight_ || y > appHeaderHeight_ + tabBarHeight_)
        {
            return false;
        }
        float left = 268.0f + 12.0f;
        const float width = 178.0f;
        for (size_t i = 0; i < workspace_->Tabs().size(); ++i)
        {
            left += width + 8.0f;
        }
        const float top = static_cast<float>(appHeaderHeight_ + 10);
        return x >= left && x <= left + 38.0f && y >= top && y <= top + 38.0f;
    }

    std::optional<int> MainWindow::HitTestSidebarButton(const int x, const int y) const
    {
        constexpr float left = 14.0f;
        constexpr float top = 78.0f;
        constexpr float size = 40.0f;
        constexpr float gap = 10.0f;
        if (y < appHeaderHeight_ + 14 || y > appHeaderHeight_ + 54)
        {
            return std::nullopt;
        }
        for (int i = 0; i < 4; ++i)
        {
            const float x0 = left + static_cast<float>(i) * (size + gap);
            if (x >= x0 && x <= x0 + size)
            {
                return i;
            }
        }
        return std::nullopt;
    }

    std::optional<size_t> MainWindow::HitTestSidebarSession(const int x, const int y) const
    {
        if (!workspace_ || x < 14 || x > 254)
        {
            return std::nullopt;
        }
        const auto visible = workspace_->FilteredSessionIndices(searchQuery_);
        const size_t shown = std::min<size_t>(5, visible.size());
        for (size_t visualIndex = 0; visualIndex < shown; ++visualIndex)
        {
            const float top = static_cast<float>(appHeaderHeight_) + 160.0f + static_cast<float>(visualIndex) * 66.0f;
            if (y >= top && y <= top + 58.0f)
            {
                return visible[visualIndex];
            }
        }
        return std::nullopt;
    }

    std::optional<int> MainWindow::HitTestShellToolbarButton(const int x, const int y) const
    {
        if (y < appHeaderHeight_ || y > appHeaderHeight_ + tabBarHeight_)
        {
            return std::nullopt;
        }
        RECT rect{};
        ::GetClientRect(hwnd_, &rect);
        const float top = static_cast<float>(appHeaderHeight_ + 9);
        float left = static_cast<float>(rect.right) - 12.0f - 3.0f * 40.0f - 8.0f;
        for (int i = 0; i < 3; ++i)
        {
            if (x >= left && x <= left + 40.0f && y >= top && y <= top + 40.0f)
            {
                return i;
            }
            left += 44.0f;
        }
        return std::nullopt;
    }

    std::optional<int> MainWindow::HitTestShellTrafficDot(const int x, const int y) const
    {
        RECT rect{};
        ::GetClientRect(hwnd_, &rect);
        const float shellLeft = 268.0f + 14.0f;
        const float shellTop = static_cast<float>(appHeaderHeight_ + tabBarHeight_) + 14.0f;
        for (int i = 0; i < 3; ++i)
        {
            const float cx = shellLeft + 22.0f + i * 18.0f;
            const float cy = shellTop + 21.0f;
            const float dx = static_cast<float>(x) - cx;
            const float dy = static_cast<float>(y) - cy;
            if (dx * dx + dy * dy <= 64.0f)
            {
                return i;
            }
        }
        return std::nullopt;
    }

    bool MainWindow::IsPointInSearchBox(const int x, const int y) const
    {
        return x >= 14 && x <= 254 && y >= appHeaderHeight_ + 68 && y <= appHeaderHeight_ + 112;
    }

    bool MainWindow::IsPointInWorkspacePill(const int x, const int y) const
    {
        RECT rect{};
        ::GetClientRect(hwnd_, &rect);
        const float buttonSize = 40.0f;
        const float rightControlsWidth = buttonSize * 3.0f + 8.0f + 12.0f;
        const float centerLeft = 150.0f;
        const float centerRight = static_cast<float>(rect.right) - rightControlsWidth - 16.0f - 10.0f;
        const float pillWidth = std::min(760.0f, std::max(360.0f, centerRight - centerLeft - 40.0f));
        const float pillLeft = centerLeft + (centerRight - centerLeft - pillWidth) * 0.5f;
        return x >= pillLeft && x <= pillLeft + pillWidth && y >= 13 && y <= 51;
    }

void MainWindow::OnLeftButtonDown(const int x, const int y)
    {
        ::SetFocus(hwnd_);
        UpdateHoverState(x, y);

        if (const auto sidebarButton = HitTestSidebarButton(x, y))
        {
            switch (*sidebarButton)
            {
            case kSidebarNewSession:
                ShowProfileMenu(x, appHeaderHeight_ + 54);
                break;
            case kSidebarWorkspaceMenu:
                ShowWorkspaceMenu(x, appHeaderHeight_ + 54);
                break;
            case kSidebarFocusSearch:
                searchFocused_ = true;
                selectionStart_.reset();
                selectionEnd_.reset();
                ::SetFocus(hwnd_);
                Invalidate();
                return;
            case kSidebarReloadSettings:
                settings_ = config::LoadSettings(SettingsPath());
                break;
            }
            UpdateWindowTitle();
            Invalidate();
            return;
        }

        if (IsPointInSearchBox(x, y))
        {
            searchFocused_ = true;
            selectionStart_.reset();
            selectionEnd_.reset();
            ::SetFocus(hwnd_);
            Invalidate();
            return;
        }
        searchFocused_ = false;

        if (IsPointInWorkspacePill(x, y) && workspace_)
        {
            ShowWorkspaceMenu(x, appHeaderHeight_);
            return;
        }

        if (const auto sessionIndex = HitTestSidebarSession(x, y))
        {
            workspace_->ActivateTab(*sessionIndex);
            FocusActiveTerminal();
            UpdateWindowTitle();
            Invalidate();
            return;
        }

        if (const auto shellButton = HitTestShellToolbarButton(x, y))
        {
            if (*shellButton == kShellPreviousSession)
            {
                ShowProfileMenu(x, appHeaderHeight_ + tabBarHeight_ + 40);
            }
            else if (*shellButton == kShellDuplicateSession)
            {
                const auto active = workspace_->ActiveTab();
                if (active != nullptr)
                {
                    for (size_t i = 0; i < settings_.profiles.size(); ++i)
                    {
                        if (settings_.profiles[i].name == active->ProfileName())
                        {
                            OpenProfile(i);
                            break;
                        }
                    }
                }
            }
            else if (*shellButton == kShellNewWorkspace)
            {
                const size_t newIndex = workspace_->AddWorkspace();
                workspace_->ActivateWorkspace(newIndex);
                OpenProfile(0);
            }
            UpdateWindowTitle();
            Invalidate();
            return;
        }

        if (const auto traffic = HitTestShellTrafficDot(x, y))
        {
            switch (*traffic)
            {
            case kShellCloseSession: CloseActiveTab(); break;
            case kShellMinimizeWindow: ::ShowWindow(hwnd_, SW_MINIMIZE); break;
            case kShellMaximizeWindow: ::ShowWindow(hwnd_, ::IsZoomed(hwnd_) ? SW_RESTORE : SW_MAXIMIZE); break;
            }
            return;
        }

        if (const auto control = HitTestWindowControl(x, y))
        {
            pressedWindowControl_ = control;
            Invalidate();
            return;
        }

        if (const auto closeIndex = HitTestTabClose(x, y))
        {
            if (workspace_->CloseTab(*closeIndex))
            {
                if (workspace_->Tabs().empty())
                {
                    OpenProfile(0);
                }
                else
                {
                    FocusActiveTerminal();
                }
                UpdateWindowTitle();
                Invalidate();
            }
            return;
        }

        if (IsPointInNewTabButton(x, y))
        {
            ShowProfileMenu(x, appHeaderHeight_ + tabBarHeight_);
            return;
        }

        if (const auto tabIndex = HitTestTab(x, y))
        {
            workspace_->ActivateTab(*tabIndex);
            FocusActiveTerminal();
            UpdateWindowTitle();
            Invalidate();
            return;
        }

        if (!IsPointInTerminal(x, y))
        {
            selectionStart_.reset();
            selectionEnd_.reset();
            Invalidate();
            return;
        }

        ::SetFocus(hwnd_);
        const auto point = ClientToBufferPoint(x, y);
        const DWORD now = ::GetTickCount();
        if (lastDoubleClickPoint_ && point.row == lastDoubleClickPoint_->row && now - lastDoubleClickTick_ <= ::GetDoubleClickTime())
        {
            selecting_ = false;
            ::ReleaseCapture();
            SelectLineAt(x, y);
            if (settings_.copyOnSelect)
            {
                CopySelection();
            }
            lastDoubleClickPoint_.reset();
            Invalidate();
            return;
        }

        ::SetCapture(hwnd_);
        selecting_ = true;
        selectionStart_ = point;
        selectionEnd_ = selectionStart_;
        Invalidate();
    }

    void MainWindow::OnMouseMove(const int x, const int y, WPARAM flags)
    {
        EnsureMouseTracking();
        const bool hoverChanged = UpdateHoverState(x, y);

        if (selecting_ && (flags & MK_LBUTTON) != 0)
        {
            UpdateSelectionForDrag(x, y);
            Invalidate();
            return;
        }

        if (hoverChanged)
        {
            Invalidate();
        }
    }

    void MainWindow::OnLeftButtonUp(const int x, const int y)
    {
        const auto releasedControl = HitTestWindowControl(x, y);
        if (pressedWindowControl_)
        {
            const int pressed = *pressedWindowControl_;
            pressedWindowControl_.reset();
            UpdateHoverState(x, y);
            Invalidate();

            if (releasedControl && *releasedControl == pressed)
            {
                switch (pressed)
                {
                case kControlMinimize:
                    ::ShowWindow(hwnd_, SW_MINIMIZE);
                    break;
                case kControlMaximize:
                    ::ShowWindow(hwnd_, ::IsZoomed(hwnd_) ? SW_RESTORE : SW_MAXIMIZE);
                    break;
                case kControlClose:
                    ::PostMessageW(hwnd_, WM_CLOSE, 0, 0);
                    break;
                }
            }
            return;
        }

        if (!selecting_)
        {
            return;
        }

        selecting_ = false;
        selectionEnd_ = ClientToBufferPoint(x, y);
        ::ReleaseCapture();
        UpdateHoverState(x, y);
        if (settings_.copyOnSelect)
        {
            CopySelection();
        }
        Invalidate();
    }

    void MainWindow::OnLeftButtonDoubleClick(const int x, const int y)
    {
        ::SetFocus(hwnd_);
        UpdateHoverState(x, y);

        if (!IsPointInTerminal(x, y))
        {
            return;
        }

        selecting_ = false;
        ::ReleaseCapture();
        SelectWordAt(x, y);
        lastDoubleClickTick_ = ::GetTickCount();
        lastDoubleClickPoint_ = ClientToBufferPoint(x, y);
        if (settings_.copyOnSelect)
        {
            CopySelection();
        }
        Invalidate();
    }

    void MainWindow::OnMiddleButtonDown(const int x, const int y)
    {
        ::SetFocus(hwnd_);
        UpdateHoverState(x, y);

        if (const auto closeIndex = HitTestTab(x, y))
        {
            if (workspace_ && workspace_->CloseTab(*closeIndex))
            {
                if (workspace_->Tabs().empty())
                {
                    ::PostQuitMessage(0);
                    return;
                }
                UpdateWindowTitle();
                Invalidate();
            }
        }
    }

    void MainWindow::OnMiddleButtonUp(const int x, const int y)
    {
        UpdateHoverState(x, y);
    }

    void MainWindow::OnMouseLeave()
    {
        mouseTracking_ = false;
        hoveredTab_.reset();
        hoveredCloseTab_.reset();
        hoveredWindowControl_.reset();
        hoveredSidebarSession_.reset();
        hoveredSidebarButton_.reset();
        hoveredShellToolbarButton_.reset();
        hoveredShellTrafficDot_.reset();
        hoverNewTabButton_ = false;
        hoverTerminal_ = false;
        hoverSearchBox_ = false;
        hoverWorkspacePill_ = false;
        if ((::GetKeyState(VK_LBUTTON) & 0x8000) == 0)
        {
            pressedWindowControl_.reset();
        }
        UpdateCursor();
        Invalidate();
    }

    bool MainWindow::UpdateHoverState(const int x, const int y)
    {
        const auto oldTab = hoveredTab_;
        const auto oldClose = hoveredCloseTab_;
        const auto oldWindowControl = hoveredWindowControl_;
        const auto oldSidebarSession = hoveredSidebarSession_;
        const auto oldSidebarButton = hoveredSidebarButton_;
        const auto oldShellButton = hoveredShellToolbarButton_;
        const auto oldTraffic = hoveredShellTrafficDot_;
        const bool oldNew = hoverNewTabButton_;
        const bool oldTerminal = hoverTerminal_;
        const bool oldSearch = hoverSearchBox_;
        const bool oldWorkspacePill = hoverWorkspacePill_;

        hoveredWindowControl_ = HitTestWindowControl(x, y);
        hoveredCloseTab_ = HitTestTabClose(x, y);
        hoveredTab_ = hoveredCloseTab_.has_value() ? hoveredCloseTab_ : HitTestTab(x, y);
        hoveredSidebarSession_ = HitTestSidebarSession(x, y);
        hoveredSidebarButton_ = HitTestSidebarButton(x, y);
        hoveredShellToolbarButton_ = HitTestShellToolbarButton(x, y);
        hoveredShellTrafficDot_ = HitTestShellTrafficDot(x, y);
        hoverNewTabButton_ = IsPointInNewTabButton(x, y);
        hoverTerminal_ = IsPointInTerminal(x, y);
        hoverSearchBox_ = IsPointInSearchBox(x, y);
        hoverWorkspacePill_ = IsPointInWorkspacePill(x, y);
        UpdateCursor();

        return hoveredTab_ != oldTab || hoveredCloseTab_ != oldClose || hoveredWindowControl_ != oldWindowControl ||
               hoveredSidebarSession_ != oldSidebarSession || hoveredSidebarButton_ != oldSidebarButton || hoveredShellToolbarButton_ != oldShellButton ||
               hoveredShellTrafficDot_ != oldTraffic || hoverNewTabButton_ != oldNew || hoverTerminal_ != oldTerminal ||
               hoverSearchBox_ != oldSearch || hoverWorkspacePill_ != oldWorkspacePill;
    }

    void MainWindow::EnsureMouseTracking()
    {
        if (mouseTracking_)
        {
            return;
        }

        TRACKMOUSEEVENT event{};
        event.cbSize = sizeof(event);
        event.dwFlags = TME_LEAVE;
        event.hwndTrack = hwnd_;
        if (::TrackMouseEvent(&event))
        {
            mouseTracking_ = true;
        }
    }

    void MainWindow::UpdateCursor()
    {
        if (selecting_ || hoverTerminal_ || hoverSearchBox_)
        {
            ::SetCursor(::LoadCursorW(nullptr, IDC_IBEAM));
            return;
        }

        if (hoveredTab_.has_value() || hoveredCloseTab_.has_value() || hoverNewTabButton_ || hoveredSidebarSession_.has_value() || hoveredSidebarButton_.has_value() || hoveredShellToolbarButton_.has_value() || hoveredShellTrafficDot_.has_value() || hoverWorkspacePill_)
        {
            ::SetCursor(::LoadCursorW(nullptr, IDC_HAND));
            return;
        }

        ::SetCursor(::LoadCursorW(nullptr, IDC_ARROW));
    }

    void MainWindow::SelectWordAt(const int x, const int y)
    {
        auto* tab = workspace_ ? workspace_->ActiveTab() : nullptr;
        if (tab == nullptr || !IsPointInTerminal(x, y))
        {
            return;
        }

        const auto point = ClientToBufferPoint(x, y);
        std::scoped_lock lock(tab->Mutex());
        const auto& lines = tab->Buffer().Lines();
        if (point.row < 0 || point.row >= static_cast<int>(lines.size()))
        {
            return;
        }

        const auto& line = lines[point.row];
        if (line.empty())
        {
            return;
        }

        const int anchor = std::clamp(point.column, 0, static_cast<int>(line.size()) - 1);
        auto isWordChar = [](const wchar_t ch)
        {
            return std::iswalnum(ch) || ch == L'_' || ch == L'-' || ch == L'.' || ch == L'/' || ch == L'\\' || ch == L':' || ch == L'~';
        };

        int left = anchor;
        int right = anchor;

        if (!isWordChar(line[anchor].glyph))
        {
            selectionStart_ = wsh::terminal::SelectionPoint{ point.row, anchor };
            selectionEnd_ = selectionStart_;
            return;
        }

        while (left > 0 && isWordChar(line[left - 1].glyph))
        {
            --left;
        }
        while (right + 1 < static_cast<int>(line.size()) && isWordChar(line[right + 1].glyph))
        {
            ++right;
        }

        selectionStart_ = wsh::terminal::SelectionPoint{ point.row, left };
        selectionEnd_ = wsh::terminal::SelectionPoint{ point.row, right };
    }

    void MainWindow::SelectLineAt(const int x, const int y)
    {
        auto* tab = workspace_ ? workspace_->ActiveTab() : nullptr;
        if (tab == nullptr || !IsPointInTerminal(x, y))
        {
            return;
        }

        const auto point = ClientToBufferPoint(x, y);
        std::scoped_lock lock(tab->Mutex());
        const auto& lines = tab->Buffer().Lines();
        if (point.row < 0 || point.row >= static_cast<int>(lines.size()))
        {
            return;
        }

        int right = static_cast<int>(lines[point.row].size()) - 1;
        while (right > 0 && (lines[point.row][right].glyph == L' ' || lines[point.row][right].glyph == L'\0'))
        {
            --right;
        }

        selectionStart_ = wsh::terminal::SelectionPoint{ point.row, 0 };
        selectionEnd_ = wsh::terminal::SelectionPoint{ point.row, std::max(0, right) };
    }

    void MainWindow::UpdateSelectionForDrag(const int x, const int y)
    {
        auto* tab = workspace_ ? workspace_->ActiveTab() : nullptr;
        if (tab == nullptr)
        {
            return;
        }

        const int terminalTop = appHeaderHeight_ + tabBarHeight_ + 14 + 42 + 12 + 24;
        const int terminalBottom = terminalTop + static_cast<int>(terminalRows_ * lineHeight_);
        if (y < terminalTop)
        {
            tab->Scroll(-1);
        }
        else if (y > terminalBottom)
        {
            tab->Scroll(1);
        }

        selectionEnd_ = ClientToBufferPoint(x, y);
    }

    void MainWindow::CopySelection()
    {
        auto* tab = workspace_ ? workspace_->ActiveTab() : nullptr;
        if (tab == nullptr || !selectionStart_ || !selectionEnd_)
        {
            return;
        }

        std::scoped_lock lock(tab->Mutex());
        const std::wstring text = tab->Buffer().CopySelection(*selectionStart_, *selectionEnd_);
        if (!text.empty())
        {
            platform::WriteClipboardText(hwnd_, text);
        }
    }

    void MainWindow::PasteClipboard()
    {
        auto* tab = workspace_ ? workspace_->ActiveTab() : nullptr;
        if (tab == nullptr)
        {
            return;
        }

        if (const auto text = platform::ReadClipboardText(hwnd_))
        {
            std::string utf8 = core::WideToUtf8(*text);
            {
                std::scoped_lock lock(tab->Mutex());
                if (tab->Buffer().IsBracketedPasteMode())
                {
                    utf8 = std::string("[200~") + utf8 + "[201~";
                }
            }
            tab->SendInput(utf8);
        }
    }

    void MainWindow::OpenProfile(const size_t index)
    {
        ResizeTerminalToClient();
        if (workspace_ && workspace_->OpenProfile(index, terminalColumns_, terminalRows_))
        {
            FocusActiveTerminal();
            UpdateWindowTitle();
            Invalidate();
        }
    }

    void MainWindow::CloseActiveTab()
    {
        if (!workspace_)
        {
            return;
        }

        workspace_->CloseActiveTab();
        if (workspace_->Tabs().empty())
        {
            OpenProfile(0);
            return;
        }

        UpdateWindowTitle();
        Invalidate();
    }

    void MainWindow::UpdateWindowTitle()
    {
        if (auto* tab = workspace_ ? workspace_->ActiveTab() : nullptr)
        {
            const std::wstring workspaceName = workspace_ ? workspace_->ActiveWorkspaceName() : L"Workspace";
            const std::wstring title = L"WSH Terminal / " + workspaceName + L" / " + tab->TitleSnapshot();
            ::SetWindowTextW(hwnd_, title.c_str());
        }
    }
}
