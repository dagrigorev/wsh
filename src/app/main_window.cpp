#include "app/main_window.h"

#include "core/logger.h"
#include "core/utf.h"
#include "platform/clipboard.h"

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

        bool IsCtrlPressed() noexcept { return (::GetKeyState(VK_CONTROL) & 0x8000) != 0; }
        bool IsShiftPressed() noexcept { return (::GetKeyState(VK_SHIFT) & 0x8000) != 0; }

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
        const int clientWidth = static_cast<int>(rect.right - rect.left);
        const int clientHeight = static_cast<int>(rect.bottom - rect.top);
        const int width = std::max(100, clientWidth - 2 * padding_ - 28);
        const int height = std::max(100, clientHeight - appHeaderHeight_ - tabBarHeight_ - statusBarHeight_ - 54);
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

        const auto slashPos = text.find_last_of(L"\/");
        if (slashPos != std::wstring::npos)
        {
            const std::wstring tail = text.substr(slashPos + 1);
            if (tail.size() + 4 <= maxChars)
            {
                const size_t headCount = maxChars - tail.size() - 1;
                return L"…" + text.substr(slashPos - std::min(slashPos, headCount) + 1, std::min(slashPos, headCount)) + L"\"" + tail;
            }

            if (tail.size() + 1 < maxChars)
            {
                return L"…\"" + tail.substr(tail.size() - (maxChars - 2));
            }
        }

        return text.substr(0, maxChars - 1) + L"…";
    }

    void MainWindow::DrawHeader()
    {
        RECT rect{};
        ::GetClientRect(hwnd_, &rect);

        D2D1_GRADIENT_STOP stops[] = {
            {0.0f, D2D1::ColorF(0.11f, 0.14f, 0.24f, 1.0f)},
            {0.36f, D2D1::ColorF(0.08f, 0.11f, 0.20f, 1.0f)},
            {1.0f, D2D1::ColorF(0.03f, 0.05f, 0.10f, 1.0f)}
        };
        ComPtr<ID2D1GradientStopCollection> stopCollection;
        renderTarget_->CreateGradientStopCollection(stops, 3, stopCollection.GetAddressOf());
        ComPtr<ID2D1LinearGradientBrush> gradient;
        renderTarget_->CreateLinearGradientBrush(
            D2D1::LinearGradientBrushProperties(D2D1::Point2F(0.0f, 0.0f), D2D1::Point2F(static_cast<float>(rect.right), static_cast<float>(rect.bottom))),
            stopCollection.Get(), gradient.GetAddressOf());
        renderTarget_->FillRectangle(MakeRect(0.0f, 0.0f, static_cast<float>(rect.right), static_cast<float>(rect.bottom)), gradient.Get());

        ComPtr<ID2D1SolidColorBrush> brush;
        renderTarget_->CreateSolidColorBrush(settings_.theme.foreground, brush.GetAddressOf());

        const auto drawSoftPanel = [&](const D2D1_RECT_F& panel, const float radius, const float fillAlpha, const float shadowAlpha)
        {
            brush->SetColor(D2D1::ColorF(0.01f, 0.02f, 0.05f, shadowAlpha));
            renderTarget_->FillRoundedRectangle(D2D1::RoundedRect(MakeRect(panel.left, panel.top + 11.0f, panel.right, panel.bottom + 12.0f), radius + 1.5f, radius + 1.5f), brush.Get());
            brush->SetColor(D2D1::ColorF(0.12f, 0.16f, 0.29f, fillAlpha));
            renderTarget_->FillRoundedRectangle(D2D1::RoundedRect(panel, radius, radius), brush.Get());
            brush->SetColor(D2D1::ColorF(1, 1, 1, 0.085f));
            renderTarget_->DrawRoundedRectangle(D2D1::RoundedRect(panel, radius, radius), brush.Get(), 1.0f);
            brush->SetColor(D2D1::ColorF(0.67f, 0.79f, 1.0f, 0.07f));
            renderTarget_->FillRoundedRectangle(D2D1::RoundedRect(MakeRect(panel.left + 1.0f, panel.top + 1.0f, panel.right - 1.0f, panel.top + 24.0f), radius - 1.0f, radius - 1.0f), brush.Get());
            brush->SetColor(D2D1::ColorF(0.00f, 0.00f, 0.00f, 0.12f));
            renderTarget_->DrawLine(D2D1::Point2F(panel.left + 6.0f, panel.bottom - 1.0f), D2D1::Point2F(panel.right - 6.0f, panel.bottom - 1.0f), brush.Get(), 1.0f);
        };

        const D2D1_RECT_F headerPanel = MakeRect(10.0f, 10.0f, static_cast<float>(rect.right) - 10.0f, static_cast<float>(appHeaderHeight_) - 8.0f);
        drawSoftPanel(headerPanel, 19.0f, 0.70f, 0.16f);

        brush->SetColor(D2D1::ColorF(1, 1, 1, 0.03f));
        renderTarget_->DrawLine(D2D1::Point2F(0.0f, static_cast<float>(appHeaderHeight_) + 1.0f), D2D1::Point2F(static_cast<float>(rect.right), static_cast<float>(appHeaderHeight_) + 1.0f), brush.Get(), 1.0f);
        brush->SetColor(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.18f));
        renderTarget_->DrawLine(D2D1::Point2F(0.0f, static_cast<float>(appHeaderHeight_) + 2.0f), D2D1::Point2F(static_cast<float>(rect.right), static_cast<float>(appHeaderHeight_) + 2.0f), brush.Get(), 1.0f);

        const D2D1_RECT_F brandRect = MakeRect(18.0f, 18.0f, 50.0f, 50.0f);
        brush->SetColor(D2D1::ColorF(0.27f, 0.16f, 0.68f, 1.0f));
        renderTarget_->FillRoundedRectangle(D2D1::RoundedRect(brandRect, 10.0f, 10.0f), brush.Get());
        brush->SetColor(D2D1::ColorF(0.34f, 0.75f, 1.0f, 1.0f));
        renderTarget_->FillRoundedRectangle(D2D1::RoundedRect(MakeRect(18.0f, 18.0f, 50.0f, 33.0f), 10.0f, 10.0f), brush.Get());
        brush->SetColor(D2D1::ColorF(1, 1, 1, 0.11f));
        renderTarget_->DrawRoundedRectangle(D2D1::RoundedRect(brandRect, 10.0f, 10.0f), brush.Get(), 1.0f);

        ComPtr<IDWriteTextFormat> titleFormat;
        dwriteFactory_->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 19.0f, L"en-US", titleFormat.GetAddressOf());
        titleFormat->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        ComPtr<IDWriteTextFormat> subtitleFormat;
        dwriteFactory_->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 12.5f, L"en-US", subtitleFormat.GetAddressOf());
        subtitleFormat->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);

        brush->SetColor(D2D1::ColorF(0.98f, 0.99f, 1.0f, 1.0f));
        renderTarget_->DrawTextW(L"WSH", 3, titleFormat.Get(), MakeRect(64.0f, 16.0f, 130.0f, 48.0f), brush.Get());

        std::wstring context = L"~/projects/wsh  •  PowerShell 7 | Admin";
        if (workspace_ && workspace_->ActiveTab())
        {
            const std::wstring active = workspace_->ActiveTab()->TitleSnapshot();
            if (!active.empty())
            {
                context = Ellipsize(active, 54);
            }
        }
        brush->SetColor(D2D1::ColorF(0.82f, 0.87f, 0.95f, 0.86f));
        renderTarget_->DrawTextW(context.c_str(), static_cast<UINT32>(context.size()), subtitleFormat.Get(), MakeRect(152.0f, 20.0f, static_cast<float>(rect.right) - 230.0f, 48.0f), brush.Get());
    }

    void MainWindow::DrawWindowControls()
    {
        RECT rect{};
        ::GetClientRect(hwnd_, &rect);

        ComPtr<ID2D1SolidColorBrush> brush;
        renderTarget_->CreateSolidColorBrush(settings_.theme.foreground, brush.GetAddressOf());

        const float buttonSize = 31.0f;
        const float gap = 7.0f;
        const float top = 17.5f;
        float left = static_cast<float>(rect.right) - (buttonSize * 3.0f + gap * 2.0f) - 18.0f;

        for (int i = 0; i < 3; ++i)
        {
            const bool hovered = hoveredWindowControl_ && *hoveredWindowControl_ == i;
            const bool pressed = pressedWindowControl_ && *pressedWindowControl_ == i;
            const D2D1_RECT_F r = MakeRect(left, top, left + buttonSize, top + buttonSize);

            const D2D1_COLOR_F fill = i == kControlClose
                ? (pressed ? D2D1::ColorF(0.74f, 0.25f, 0.31f, 0.98f) : hovered ? D2D1::ColorF(0.78f, 0.28f, 0.34f, 0.94f) : D2D1::ColorF(0.17f, 0.20f, 0.30f, 0.92f))
                : (pressed ? D2D1::ColorF(0.18f, 0.22f, 0.36f, 0.98f) : hovered ? D2D1::ColorF(0.19f, 0.24f, 0.38f, 0.95f) : D2D1::ColorF(0.13f, 0.16f, 0.25f, 0.88f));
            const D2D1_COLOR_F stroke = i == kControlClose
                ? D2D1::ColorF(1, 1, 1, hovered ? 0.16f : 0.09f)
                : D2D1::ColorF(1, 1, 1, hovered ? 0.13f : 0.07f);

            brush->SetColor(D2D1::ColorF(0.01f, 0.02f, 0.05f, 0.14f));
            renderTarget_->FillRoundedRectangle(D2D1::RoundedRect(MakeRect(r.left, r.top + 6.0f, r.right, r.bottom + 6.0f), 10.0f, 10.0f), brush.Get());
            brush->SetColor(fill);
            renderTarget_->FillRoundedRectangle(D2D1::RoundedRect(r, 10.0f, 10.0f), brush.Get());
            brush->SetColor(stroke);
            renderTarget_->DrawRoundedRectangle(D2D1::RoundedRect(r, 10.0f, 10.0f), brush.Get(), 1.0f);
            brush->SetColor(D2D1::ColorF(1, 1, 1, hovered ? 0.075f : 0.04f));
            renderTarget_->FillRoundedRectangle(D2D1::RoundedRect(MakeRect(r.left + 1.0f, r.top + 1.0f, r.right - 1.0f, r.top + 10.0f), 9.0f, 9.0f), brush.Get());

            brush->SetColor(D2D1::ColorF(0.96f, 0.98f, 1.0f, 0.98f));
            const float cx = (r.left + r.right) * 0.5f;
            const float cy = (r.top + r.bottom) * 0.5f;
            switch (i)
            {
            case kControlMinimize:
                renderTarget_->DrawLine(D2D1::Point2F(cx - 5.5f, cy + 4.0f), D2D1::Point2F(cx + 5.5f, cy + 4.0f), brush.Get(), 1.7f);
                break;
            case kControlMaximize:
                if (::IsZoomed(hwnd_))
                {
                    renderTarget_->DrawRectangle(MakeRect(cx - 5.0f, cy - 2.5f, cx + 4.0f, cy + 6.5f), brush.Get(), 1.3f);
                    renderTarget_->DrawRectangle(MakeRect(cx - 2.0f, cy - 5.5f, cx + 7.0f, cy + 3.5f), brush.Get(), 1.3f);
                }
                else
                {
                    renderTarget_->DrawRectangle(MakeRect(cx - 5.5f, cy - 5.5f, cx + 5.5f, cy + 5.5f), brush.Get(), 1.45f);
                }
                break;
            case kControlClose:
                renderTarget_->DrawLine(D2D1::Point2F(cx - 5.0f, cy - 5.0f), D2D1::Point2F(cx + 5.0f, cy + 5.0f), brush.Get(), 1.75f);
                renderTarget_->DrawLine(D2D1::Point2F(cx + 5.0f, cy - 5.0f), D2D1::Point2F(cx - 5.0f, cy + 5.0f), brush.Get(), 1.75f);
                break;
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

        ComPtr<ID2D1SolidColorBrush> brush;
        renderTarget_->CreateSolidColorBrush(settings_.theme.muted, brush.GetAddressOf());

        const D2D1_RECT_F stripRect = MakeRect(static_cast<float>(padding_) - 4.0f, static_cast<float>(appHeaderHeight_ + 8), static_cast<float>(padding_) + 784.0f, static_cast<float>(appHeaderHeight_ + tabBarHeight_ - 2));
        brush->SetColor(D2D1::ColorF(1, 1, 1, 0.026f));
        renderTarget_->FillRoundedRectangle(D2D1::RoundedRect(stripRect, 18.0f, 18.0f), brush.Get());
        brush->SetColor(D2D1::ColorF(0.66f, 0.78f, 1.0f, 0.05f));
        renderTarget_->FillRoundedRectangle(D2D1::RoundedRect(MakeRect(stripRect.left + 1.0f, stripRect.top + 1.0f, stripRect.right - 1.0f, stripRect.top + 15.0f), 17.0f, 17.0f), brush.Get());

        const auto drawPill = [&](const D2D1_RECT_F& r, const D2D1_COLOR_F& fill, const D2D1_COLOR_F& stroke, const float glowAlpha, const bool active)
        {
            brush->SetColor(D2D1::ColorF(0.01f, 0.02f, 0.05f, glowAlpha));
            renderTarget_->FillRoundedRectangle(D2D1::RoundedRect(MakeRect(r.left, r.top + 7.0f, r.right, r.bottom + 8.0f), 16.0f, 16.0f), brush.Get());
            brush->SetColor(fill);
            renderTarget_->FillRoundedRectangle(D2D1::RoundedRect(r, 16.0f, 16.0f), brush.Get());
            brush->SetColor(stroke);
            renderTarget_->DrawRoundedRectangle(D2D1::RoundedRect(r, 16.0f, 16.0f), brush.Get(), 1.0f);
            brush->SetColor(active ? D2D1::ColorF(0.70f, 0.56f, 1.0f, 0.17f) : D2D1::ColorF(1, 1, 1, 0.035f));
            renderTarget_->FillRoundedRectangle(D2D1::RoundedRect(MakeRect(r.left + 1.0f, r.top + 1.0f, r.right - 1.0f, r.top + 12.0f), 15.0f, 15.0f), brush.Get());
            brush->SetColor(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.10f));
            renderTarget_->DrawLine(D2D1::Point2F(r.left + 10.0f, r.bottom - 1.0f), D2D1::Point2F(r.right - 10.0f, r.bottom - 1.0f), brush.Get(), 1.0f);
        };

        float x = static_cast<float>(padding_);
        const float top = static_cast<float>(appHeaderHeight_ + 14);
        const float height = 44.0f;
        const float width = 178.0f;
        const auto& tabs = workspace_->Tabs();

        for (size_t i = 0; i < tabs.size(); ++i)
        {
            const bool active = i == workspace_->ActiveIndex();
            const bool hovered = hoveredTab_ && *hoveredTab_ == i;
            const bool closeHovered = hoveredCloseTab_ && *hoveredCloseTab_ == i;
            const D2D1_RECT_F tabRect = MakeRect(x, top, x + width, top + height);
            drawPill(tabRect,
                active ? D2D1::ColorF(0.30f, 0.21f, 0.52f, 0.99f) : (hovered ? D2D1::ColorF(0.16f, 0.19f, 0.29f, 0.98f) : D2D1::ColorF(0.13f, 0.16f, 0.24f, 0.95f)),
                active ? D2D1::ColorF(0.67f, 0.43f, 1.0f, 0.75f) : D2D1::ColorF(1, 1, 1, hovered ? 0.10f : 0.055f),
                active ? 0.18f : 0.10f,
                active);

            brush->SetColor(active ? D2D1::ColorF(0.69f, 0.46f, 1.0f, 0.90f) : D2D1::ColorF(1, 1, 1, 0.03f));
            renderTarget_->FillRoundedRectangle(D2D1::RoundedRect(MakeRect(x + 10.0f, top + height - 4.0f, x + width - 10.0f, top + height - 1.0f), 2.0f, 2.0f), brush.Get());

            brush->SetColor(active ? D2D1::ColorF(0.98f, 0.99f, 1.0f, 1.0f) : D2D1::ColorF(0.83f, 0.86f, 0.92f, 0.95f));
            const std::wstring tabTitle = Ellipsize(BuildTabLabel(i), 13);
            renderTarget_->DrawTextW(tabTitle.c_str(), static_cast<UINT32>(tabTitle.size()), uiFormat_.Get(), MakeRect(x + 18.0f, top + 9.0f, x + width - 48.0f, top + height), brush.Get());

            const float closeSize = 22.0f;
            const float closeLeft = x + width - 34.0f;
            const float closeTop = top + (height - closeSize) * 0.5f;
            const D2D1_RECT_F closeRect = MakeRect(closeLeft, closeTop, closeLeft + closeSize, closeTop + closeSize);
            brush->SetColor(closeHovered ? D2D1::ColorF(0.44f, 0.22f, 0.29f, 0.92f) : D2D1::ColorF(1, 1, 1, active ? 0.055f : 0.032f));
            renderTarget_->FillRoundedRectangle(D2D1::RoundedRect(closeRect, 11.0f, 11.0f), brush.Get());
            brush->SetColor(closeHovered ? D2D1::ColorF(1.0f, 0.72f, 0.74f, 1.0f) : D2D1::ColorF(0.82f, 0.85f, 0.92f, 0.92f));
            const float cx = closeLeft + closeSize * 0.5f;
            const float cy = closeTop + closeSize * 0.5f;
            renderTarget_->DrawLine(D2D1::Point2F(cx - 4.0f, cy - 4.0f), D2D1::Point2F(cx + 4.0f, cy + 4.0f), brush.Get(), 1.45f);
            renderTarget_->DrawLine(D2D1::Point2F(cx + 4.0f, cy - 4.0f), D2D1::Point2F(cx - 4.0f, cy + 4.0f), brush.Get(), 1.45f);

            x += width + 12.0f;
        }

        const D2D1_RECT_F newRect = MakeRect(x, top, x + 44.0f, top + height);
        drawPill(newRect,
            hoverNewTabButton_ ? D2D1::ColorF(0.17f, 0.20f, 0.30f, 0.96f) : D2D1::ColorF(0.13f, 0.16f, 0.24f, 0.92f),
            D2D1::ColorF(1, 1, 1, hoverNewTabButton_ ? 0.10f : 0.055f),
            0.10f,
            false);
        brush->SetColor(D2D1::ColorF(0.97f, 0.99f, 1.0f, 1.0f));
        const float cx = x + 22.0f;
        const float cy = top + height * 0.5f;
        renderTarget_->DrawLine(D2D1::Point2F(cx - 5.0f, cy), D2D1::Point2F(cx + 5.0f, cy), brush.Get(), 1.7f);
        renderTarget_->DrawLine(D2D1::Point2F(cx, cy - 5.0f), D2D1::Point2F(cx, cy + 5.0f), brush.Get(), 1.7f);
    }

    void MainWindow::DrawTerminal()
    {
        auto* tab = workspace_ ? workspace_->ActiveTab() : nullptr;
        if (tab == nullptr)
        {
            return;
        }

        const float cardLeft = static_cast<float>(padding_);
        const float cardTop = static_cast<float>(appHeaderHeight_ + tabBarHeight_ + 22);
        const float cardRight = cardLeft + terminalColumns_ * charWidth_ + 32.0f;
        const float cardBottom = cardTop + terminalRows_ * lineHeight_ + statusBarHeight_ + 42.0f;
        const float left = cardLeft + 18.0f;
        const float top = cardTop + 18.0f;
        const float bottom = top + terminalRows_ * lineHeight_ + 8.0f;

        ComPtr<ID2D1SolidColorBrush> brush;
        renderTarget_->CreateSolidColorBrush(settings_.theme.foreground, brush.GetAddressOf());

        brush->SetColor(D2D1::ColorF(0.01f, 0.02f, 0.05f, 0.16f));
        renderTarget_->FillRoundedRectangle(D2D1::RoundedRect(MakeRect(cardLeft, cardTop + 12.0f, cardRight, cardBottom + 12.0f), 24.0f, 24.0f), brush.Get());
        brush->SetColor(D2D1::ColorF(0.09f, 0.12f, 0.21f, 0.94f));
        renderTarget_->FillRoundedRectangle(D2D1::RoundedRect(MakeRect(cardLeft, cardTop, cardRight, cardBottom), 24.0f, 24.0f), brush.Get());
        brush->SetColor(D2D1::ColorF(1, 1, 1, 0.065f));
        renderTarget_->DrawRoundedRectangle(D2D1::RoundedRect(MakeRect(cardLeft, cardTop, cardRight, cardBottom), 24.0f, 24.0f), brush.Get(), 1.0f);
        brush->SetColor(D2D1::ColorF(0.66f, 0.78f, 1.0f, 0.030f));
        renderTarget_->FillRoundedRectangle(D2D1::RoundedRect(MakeRect(cardLeft + 1.0f, cardTop + 1.0f, cardRight - 1.0f, cardTop + 24.0f), 23.0f, 23.0f), brush.Get());
        brush->SetColor(D2D1::ColorF(1, 1, 1, 0.025f));
        renderTarget_->DrawRoundedRectangle(D2D1::RoundedRect(MakeRect(cardLeft + 1.0f, cardTop + 1.0f, cardRight - 1.0f, cardBottom - 1.0f), 23.0f, 23.0f), brush.Get(), 1.0f);

        brush->SetColor(D2D1::ColorF(0.04f, 0.07f, 0.13f, 0.99f));
        renderTarget_->FillRoundedRectangle(D2D1::RoundedRect(MakeRect(left - 8.0f, top - 8.0f, left + terminalColumns_ * charWidth_ + 12.0f, bottom + 8.0f), 18.0f, 18.0f), brush.Get());
        brush->SetColor(D2D1::ColorF(1, 1, 1, 0.035f));
        renderTarget_->DrawRoundedRectangle(D2D1::RoundedRect(MakeRect(left - 8.0f, top - 8.0f, left + terminalColumns_ * charWidth_ + 12.0f, bottom + 8.0f), 18.0f, 18.0f), brush.Get(), 1.0f);

        renderTarget_->PushAxisAlignedClip(MakeRect(left, top, left + terminalColumns_ * charWidth_, top + terminalRows_ * lineHeight_), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

        const auto selectionLeft = selectionStart_ && selectionEnd_ ? std::min(*selectionStart_, *selectionEnd_) : wsh::terminal::SelectionPoint{};
        const auto selectionRight = selectionStart_ && selectionEnd_ ? std::max(*selectionStart_, *selectionEnd_) : wsh::terminal::SelectionPoint{};
        const bool hasSelection = selectionStart_.has_value() && selectionEnd_.has_value();

        std::scoped_lock lock(tab->Mutex());
        const auto& lines = tab->Buffer().Lines();
        const int viewportTop = tab->Buffer().ViewportTop();
        for (int row = 0; row < terminalRows_; ++row)
        {
            const int bufferRow = viewportTop + row;
            if (bufferRow >= static_cast<int>(lines.size()))
            {
                break;
            }

            const float y = top + row * lineHeight_;
            for (int column = 0; column < terminalColumns_ && column < static_cast<int>(lines[bufferRow].size()); ++column)
            {
                const auto& cell = lines[bufferRow][column];
                const float x = left + column * charWidth_;
                const bool selected = hasSelection && wsh::terminal::SelectionPoint{ bufferRow, column } >= selectionLeft && wsh::terminal::SelectionPoint{ bufferRow, column } <= selectionRight;

                D2D1_COLOR_F background = cell.background;
                D2D1_COLOR_F foreground = cell.foreground;
                if (cell.inverse)
                {
                    std::swap(background, foreground);
                }

                if (background.a > 0.01f)
                {
                    brush->SetColor(background);
                    renderTarget_->FillRectangle(MakeRect(x, y, x + charWidth_, y + lineHeight_), brush.Get());
                }

                if (selected)
                {
                    brush->SetColor(D2D1::ColorF(0.28f, 0.46f, 0.86f, 0.82f));
                    renderTarget_->FillRectangle(MakeRect(x, y, x + charWidth_, y + lineHeight_), brush.Get());
                    foreground = D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f);
                }

                brush->SetColor(foreground);
                const wchar_t glyph[2] = { cell.glyph == L'\0' ? L' ' : cell.glyph, 0 };
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
            brush->SetColor(settings_.theme.accent);
            const int relativeRow = cursor.row - viewportTop;
            if (relativeRow >= 0 && relativeRow < terminalRows_)
            {
                const float x = left + cursor.column * charWidth_;
                const float y = top + relativeRow * lineHeight_;
                switch (settings_.cursorStyle)
                {
                case config::CursorStyle::Block:
                    renderTarget_->DrawRectangle(MakeRect(x + 0.5f, y + 0.5f, x + charWidth_ - 0.5f, y + lineHeight_ - 0.5f), brush.Get(), 1.5f);
                    break;
                case config::CursorStyle::Underline:
                    renderTarget_->FillRectangle(MakeRect(x, y + lineHeight_ - 3.0f, x + charWidth_, y + lineHeight_ - 1.0f), brush.Get());
                    break;
                case config::CursorStyle::Bar:
                default:
                    renderTarget_->FillRectangle(MakeRect(x, y + 2.0f, x + 2.0f, y + lineHeight_ - 2.0f), brush.Get());
                    break;
                }
            }
        }

        renderTarget_->PopAxisAlignedClip();
    }

    void MainWindow::DrawStatusBar()
    {
        RECT rect{};
        ::GetClientRect(hwnd_, &rect);

        const float cardLeft = static_cast<float>(padding_) - 4.0f;
        const float cardTop = static_cast<float>(appHeaderHeight_ + tabBarHeight_ + 14);
        const float cardRight = static_cast<float>(rect.right - padding_ + 4);
        const float cardBottom = static_cast<float>(rect.bottom - padding_ + 2);
        const D2D1_RECT_F cardRect = MakeRect(cardLeft, cardTop, cardRight, cardBottom);

        ComPtr<ID2D1SolidColorBrush> brush;
        renderTarget_->CreateSolidColorBrush(settings_.theme.foreground, brush.GetAddressOf());

        brush->SetColor(D2D1::ColorF(0.01f, 0.02f, 0.05f, 0.16f));
        renderTarget_->FillRoundedRectangle(D2D1::RoundedRect(MakeRect(cardRect.left, cardRect.top + 12.0f, cardRect.right, cardRect.bottom + 10.0f), 24.0f, 24.0f), brush.Get());
        brush->SetColor(D2D1::ColorF(0.12f, 0.15f, 0.27f, 0.18f));
        renderTarget_->DrawRoundedRectangle(D2D1::RoundedRect(cardRect, 24.0f, 24.0f), brush.Get(), 1.0f);

        const float statusHeight = 31.0f;
        const D2D1_RECT_F statusRect = MakeRect(cardRect.left + 12.0f, cardRect.bottom - statusHeight - 10.0f, cardRect.right - 12.0f, cardRect.bottom - 12.0f);
        brush->SetColor(D2D1::ColorF(0.12f, 0.16f, 0.29f, 0.56f));
        renderTarget_->FillRoundedRectangle(D2D1::RoundedRect(statusRect, 15.0f, 15.0f), brush.Get());
        brush->SetColor(D2D1::ColorF(1, 1, 1, 0.035f));
        renderTarget_->DrawRoundedRectangle(D2D1::RoundedRect(statusRect, 15.0f, 15.0f), brush.Get(), 1.0f);
        brush->SetColor(D2D1::ColorF(0.66f, 0.78f, 1.0f, 0.035f));
        renderTarget_->FillRoundedRectangle(D2D1::RoundedRect(MakeRect(statusRect.left + 1.0f, statusRect.top + 1.0f, statusRect.right - 1.0f, statusRect.top + 12.0f), 14.0f, 14.0f), brush.Get());

        std::wstring status = std::format(L"{}  |  UTF-8  |  LF  |  x64  |  Ready  |  main", workspace_ && workspace_->ActiveTab() ? workspace_->ActiveTab()->ProfileName() : L"shell");
        brush->SetColor(D2D1::ColorF(0.92f, 0.95f, 1.0f, 0.96f));
        renderTarget_->DrawTextW(status.c_str(), static_cast<UINT32>(status.size()), uiFormat_.Get(), MakeRect(statusRect.left + 12.0f, statusRect.top + 5.0f, statusRect.right - 12.0f, statusRect.bottom), brush.Get());
    }

    void MainWindow::OnChar(const wchar_t ch)
    {
        auto* tab = workspace_ ? workspace_->ActiveTab() : nullptr;
        if (tab == nullptr)
        {
            return;
        }

        if (IsCtrlPressed())
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

void MainWindow::OnKeyDown(const WPARAM key, LPARAM)
    {
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
        case VK_HOME:
            tab->SendInput("\x1b[H");
            break;
        case VK_END:
            tab->SendInput("\x1b[F");
            break;
        case VK_DELETE:
            tab->SendInput("\x1b[3~");
            break;
        case VK_ESCAPE:
            tab->SendInput("\x1b");
            break;
        case VK_PRIOR:
            tab->Scroll(-terminalRows_ / 2);
            Invalidate();
            return;
        case VK_NEXT:
            tab->Scroll(terminalRows_ / 2);
            Invalidate();
            return;
        case VK_RETURN:
            tab->SendInput("\r");
            break;
        case VK_BACK:
            tab->SendInput("\b");
            break;
        case VK_TAB:
            tab->SendInput(IsShiftPressed() ? "\x1b[Z" : "\t");
            break;
        case VK_LEFT:
            tab->SendInput("\x1b[D");
            break;
        case VK_RIGHT:
            tab->SendInput("\x1b[C");
            break;
        case VK_UP:
            tab->SendInput("\x1b[A");
            break;
        case VK_DOWN:
            tab->SendInput("\x1b[B");
            break;
        default:
            return;
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
        const int left = padding_ + 18;
        const int top = appHeaderHeight_ + tabBarHeight_ + padding_ + 18;
        const int right = left + static_cast<int>(terminalColumns_ * charWidth_);
        const int bottom = top + static_cast<int>(terminalRows_ * lineHeight_);
        return x >= left && x <= right && y >= top && y <= bottom;
    }

    bool MainWindow::IsPointInDraggableHeader(const int x, const int y) const
    {
        if (y < 10 || y > appHeaderHeight_ - 6)
        {
            return false;
        }

        if (HitTestWindowControl(x, y).has_value())
        {
            return false;
        }

        if (x >= padding_ && x <= 360)
        {
            return true;
        }

        return x > 120 && x < 900;
    }

    std::optional<int> MainWindow::HitTestWindowControl(const int x, const int y) const
    {
        RECT rect{};
        ::GetClientRect(hwnd_, &rect);

        const float buttonSize = 34.0f;
        const float gap = 10.0f;
        const float top = 16.0f;
        float left = static_cast<float>(rect.right) - (buttonSize * 3.0f + gap * 2.0f) - 20.0f;

        for (int i = 0; i < 3; ++i)
        {
            if (x >= left && x <= left + buttonSize && y >= top && y <= top + buttonSize)
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

        const int column = std::clamp(static_cast<int>((x - padding_ - 18) / charWidth_), 0, terminalColumns_ - 1);
        const int row = std::clamp(static_cast<int>((y - appHeaderHeight_ - tabBarHeight_ - padding_ - 18) / lineHeight_), 0, terminalRows_ - 1);
        return { tab->Buffer().ViewportTop() + row, column };
    }

    std::optional<size_t> MainWindow::HitTestTab(const int x, const int y) const
    {
        if (!workspace_ || y < appHeaderHeight_ || y > appHeaderHeight_ + tabBarHeight_ + 20)
        {
            return std::nullopt;
        }

        float left = static_cast<float>(padding_);
        const float top = static_cast<float>(appHeaderHeight_ + 12);
        const float height = 44.0f;
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
        if (!workspace_ || y < appHeaderHeight_ || y > appHeaderHeight_ + tabBarHeight_ + 20)
        {
            return std::nullopt;
        }

        float left = static_cast<float>(padding_);
        const float top = static_cast<float>(appHeaderHeight_ + 12);
        const float width = 178.0f;
        const float height = 44.0f;
        for (size_t i = 0; i < workspace_->Tabs().size(); ++i)
        {
            const float closeLeft = left + width - 34.0f;
            const float closeRight = closeLeft + 22.0f;
            const float closeTop = top + (height - 22.0f) * 0.5f;
            const float closeBottom = closeTop + 22.0f;
            if (x >= closeLeft && x <= closeRight && y >= closeTop && y <= closeBottom)
            {
                return i;
            }
            left += width + 8.0f;
        }

        return std::nullopt;
    }

    bool MainWindow::IsPointInNewTabButton(const int x, const int y) const
    {
        if (!workspace_ || y < appHeaderHeight_ || y > appHeaderHeight_ + tabBarHeight_ + 20)
        {
            return false;
        }

        float left = static_cast<float>(padding_);
        const float width = 178.0f;
        for (size_t i = 0; i < workspace_->Tabs().size(); ++i)
        {
            left += width + 8.0f;
        }

        const float top = static_cast<float>(appHeaderHeight_ + 12);
        return x >= left && x <= left + 48.0f && y >= top && y <= top + 46.0f;
    }

void MainWindow::OnLeftButtonDown(const int x, const int y)
    {
        ::SetFocus(hwnd_);
        UpdateHoverState(x, y);

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
                    ::PostQuitMessage(0);
                    return;
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
        hoverNewTabButton_ = false;
        hoverTerminal_ = false;
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
        const bool oldNew = hoverNewTabButton_;
        const bool oldTerminal = hoverTerminal_;

        hoveredWindowControl_ = HitTestWindowControl(x, y);
        hoveredCloseTab_ = HitTestTabClose(x, y);
        hoveredTab_ = hoveredCloseTab_.has_value() ? hoveredCloseTab_ : HitTestTab(x, y);
        hoverNewTabButton_ = IsPointInNewTabButton(x, y);
        hoverTerminal_ = IsPointInTerminal(x, y);
        UpdateCursor();

        return hoveredTab_ != oldTab || hoveredCloseTab_ != oldClose || hoveredWindowControl_ != oldWindowControl || hoverNewTabButton_ != oldNew || hoverTerminal_ != oldTerminal;
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
        if (selecting_ || hoverTerminal_)
        {
            ::SetCursor(::LoadCursorW(nullptr, IDC_IBEAM));
            return;
        }

        if (hoveredTab_.has_value() || hoveredCloseTab_.has_value() || hoverNewTabButton_)
        {
            ::SetCursor(::LoadCursorW(nullptr, IDC_HAND));
            return;
        }

        if (hoveredWindowControl_.has_value())
        {
            ::SetCursor(::LoadCursorW(nullptr, IDC_ARROW));
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

        const int terminalTop = tabBarHeight_ + padding_;
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
            tab->SendInput(core::WideToUtf8(*text));
        }
    }

    void MainWindow::OpenProfile(const size_t index)
    {
        ResizeTerminalToClient();
        if (workspace_ && workspace_->OpenProfile(index, terminalColumns_, terminalRows_))
        {
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
            ::PostQuitMessage(0);
            return;
        }

        UpdateWindowTitle();
        Invalidate();
    }

    void MainWindow::UpdateWindowTitle()
    {
        if (auto* tab = workspace_ ? workspace_->ActiveTab() : nullptr)
        {
            const std::wstring title = L"WSH Terminal / " + tab->TitleSnapshot();
            ::SetWindowTextW(hwnd_, title.c_str());
        }
    }
}
