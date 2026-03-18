#include "app/main_window.h"

#include "core/logger.h"
#include "core/utf.h"
#include "platform/clipboard.h"

#include <algorithm>
#include <format>
#include <string>
#include <windowsx.h>

using Microsoft::WRL::ComPtr;

namespace wsh::app
{
    namespace
    {
        constexpr wchar_t kWindowClassName[] = L"WSH.Terminal.MainWindow";

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
        windowClass.lpfnWndProc = &MainWindow::WindowProc;
        windowClass.hInstance = instance;
        windowClass.hCursor = ::LoadCursorW(nullptr, IDC_IBEAM);
        windowClass.hbrBackground = nullptr;
        windowClass.lpszClassName = kWindowClassName;

        ::RegisterClassExW(&windowClass);

        hwnd_ = ::CreateWindowExW(
            0,
            kWindowClassName,
            L"WSH Terminal",
            WS_OVERLAPPEDWINDOW | WS_VISIBLE,
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
        case WM_LBUTTONUP:
            OnLeftButtonUp(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
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
        const int width = std::max(100, clientWidth - 2 * padding_);
        const int height = std::max(100, clientHeight - tabBarHeight_ - statusBarHeight_ - 2 * padding_);
        terminalColumns_ = std::max(20, static_cast<int>(width / charWidth_));
        terminalRows_ = std::max(8, static_cast<int>(height / lineHeight_));

        if (auto* tab = workspace_ ? workspace_->ActiveTab() : nullptr)
        {
            tab->Resize(terminalColumns_, terminalRows_);
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

    void MainWindow::DrawTabs()
    {
        if (!workspace_)
        {
            return;
        }

        ComPtr<ID2D1SolidColorBrush> brush;
        renderTarget_->CreateSolidColorBrush(settings_.theme.muted, brush.GetAddressOf());

        float x = static_cast<float>(padding_);
        const float top = 8.0f;
        const float height = static_cast<float>(tabBarHeight_ - 10);

        const auto& tabs = workspace_->Tabs();
        for (size_t i = 0; i < tabs.size(); ++i)
        {
            const float width = 170.0f;
            const bool active = i == workspace_->ActiveIndex();

            brush->SetColor(active ? settings_.theme.statusBackground : D2D1::ColorF(0.10f, 0.13f, 0.22f, 1.0f));
            renderTarget_->FillRoundedRectangle(D2D1::RoundedRect(MakeRect(x, top, x + width, top + height), 10.0f, 10.0f), brush.Get());

            brush->SetColor(active ? settings_.theme.accent : settings_.theme.muted);
            renderTarget_->FillRectangle(MakeRect(x, top + height - 3.0f, x + width, top + height), brush.Get());

            brush->SetColor(settings_.theme.foreground);
            const std::wstring tabTitle = tabs[i]->TitleSnapshot();
            renderTarget_->DrawTextW(tabTitle.c_str(), static_cast<UINT32>(tabTitle.size()), uiFormat_.Get(), MakeRect(x + 14.0f, top + 7.0f, x + width - 12.0f, top + height), brush.Get());

            x += width + 8.0f;
        }
    }

    void MainWindow::DrawTerminal()
    {
        auto* tab = workspace_ ? workspace_->ActiveTab() : nullptr;
        if (tab == nullptr)
        {
            return;
        }

        const float left = static_cast<float>(padding_);
        const float top = static_cast<float>(tabBarHeight_ + padding_);
        const float bottom = top + terminalRows_ * lineHeight_ + 8.0f;

        ComPtr<ID2D1SolidColorBrush> brush;
        renderTarget_->CreateSolidColorBrush(settings_.theme.foreground, brush.GetAddressOf());

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

                if (selected)
                {
                    brush->SetColor(settings_.theme.selection);
                    renderTarget_->FillRectangle(MakeRect(x, y, x + charWidth_, y + lineHeight_), brush.Get());
                }

                brush->SetColor(cell.foreground);
                const wchar_t glyph[2] = { cell.glyph == L'\0' ? L' ' : cell.glyph, 0 };
                renderTarget_->DrawTextW(glyph, 1, terminalFormat_.Get(), MakeRect(x, y, x + charWidth_ * 2.0f, y + lineHeight_), brush.Get());
            }
        }

        const auto& cursor = tab->Buffer().GetCursor();
        if (cursor.visible)
        {
            brush->SetColor(settings_.theme.accent);
            const int relativeRow = cursor.row - viewportTop;
            if (relativeRow >= 0 && relativeRow < terminalRows_)
            {
                const float x = left + cursor.column * charWidth_;
                const float y = top + relativeRow * lineHeight_;
                renderTarget_->DrawRectangle(MakeRect(x, y, x + charWidth_, y + lineHeight_), brush.Get(), 1.5f);
            }
        }

        brush->SetColor(D2D1::ColorF(1, 1, 1, 0.05f));
        renderTarget_->DrawRectangle(MakeRect(left - 4.0f, top - 4.0f, left + terminalColumns_ * charWidth_ + 4.0f, bottom), brush.Get(), 1.0f);
    }

    void MainWindow::DrawStatusBar()
    {
        auto* tab = workspace_ ? workspace_->ActiveTab() : nullptr;
        if (tab == nullptr)
        {
            return;
        }

        RECT rect{};
        ::GetClientRect(hwnd_, &rect);
        const float top = static_cast<float>(rect.bottom - statusBarHeight_);

        ComPtr<ID2D1SolidColorBrush> brush;
        renderTarget_->CreateSolidColorBrush(settings_.theme.statusBackground, brush.GetAddressOf());
        renderTarget_->FillRectangle(MakeRect(0.0f, top, static_cast<float>(rect.right), static_cast<float>(rect.bottom)), brush.Get());

        brush->SetColor(settings_.theme.foreground);
        const std::wstring activeTitle = tab->TitleSnapshot();
        std::wstring text = std::format(L"Профиль: {}    {}x{}    Вкладки: {}    Ctrl+Shift+C/V • Ctrl+T • F1/F2/F3",
            activeTitle, terminalColumns_, terminalRows_, workspace_->Tabs().size());
        renderTarget_->DrawTextW(text.c_str(), static_cast<UINT32>(text.size()), uiFormat_.Get(), MakeRect(12.0f, top + 6.0f, static_cast<float>(rect.right) - 12.0f, static_cast<float>(rect.bottom) - 4.0f), brush.Get());
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

    void MainWindow::OnKeyDown(const WPARAM key, LPARAM)
    {
        auto* tab = workspace_ ? workspace_->ActiveTab() : nullptr;
        if (!workspace_ || tab == nullptr)
        {
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
        if (IsCtrlPressed() && IsShiftPressed() && key == 'C')
        {
            CopySelection();
            return;
        }
        if (IsCtrlPressed() && IsShiftPressed() && key == 'V')
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
            tab->SendInput("\t");
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
            tab->Scroll(delta > 0 ? -3 : 3);
            Invalidate();
        }
    }

    bool MainWindow::IsPointInTerminal(const int x, const int y) const
    {
        const int left = padding_;
        const int top = tabBarHeight_ + padding_;
        const int right = left + static_cast<int>(terminalColumns_ * charWidth_);
        const int bottom = top + static_cast<int>(terminalRows_ * lineHeight_);
        return x >= left && x <= right && y >= top && y <= bottom;
    }

    wsh::terminal::SelectionPoint MainWindow::ClientToBufferPoint(const int x, const int y) const
    {
        auto* tab = workspace_ ? workspace_->ActiveTab() : nullptr;
        if (tab == nullptr)
        {
            return {};
        }

        const int column = std::clamp(static_cast<int>((x - padding_) / charWidth_), 0, terminalColumns_ - 1);
        const int row = std::clamp(static_cast<int>((y - tabBarHeight_ - padding_) / lineHeight_), 0, terminalRows_ - 1);
        return { tab->Buffer().ViewportTop() + row, column };
    }

    std::optional<size_t> MainWindow::HitTestTab(const int x, const int y) const
    {
        if (!workspace_ || y < 8 || y > tabBarHeight_)
        {
            return std::nullopt;
        }

        float left = static_cast<float>(padding_);
        const float top = 8.0f;
        const float height = static_cast<float>(tabBarHeight_ - 10);
        const float width = 170.0f;

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

    void MainWindow::OnLeftButtonDown(const int x, const int y)
    {
        ::SetFocus(hwnd_);

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

        ::SetCapture(hwnd_);
        selecting_ = true;
        selectionStart_ = ClientToBufferPoint(x, y);
        selectionEnd_ = selectionStart_;
        Invalidate();
    }

    void MainWindow::OnMouseMove(const int x, const int y, WPARAM flags)
    {
        if (selecting_ && (flags & MK_LBUTTON) != 0)
        {
            selectionEnd_ = ClientToBufferPoint(x, y);
            Invalidate();
        }
    }

    void MainWindow::OnLeftButtonUp(const int x, const int y)
    {
        if (!selecting_)
        {
            return;
        }

        selecting_ = false;
        selectionEnd_ = ClientToBufferPoint(x, y);
        ::ReleaseCapture();
        Invalidate();
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
