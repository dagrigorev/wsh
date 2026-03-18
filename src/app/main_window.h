#pragma once

#include "config/settings.h"
#include "terminal/screen_buffer.h"
#include "workspace/workspace.h"

#include <Windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <optional>
#include <string>

namespace wsh::app
{
    class MainWindow
    {
    public:
        bool Create(HINSTANCE instance, int showCommand);

    private:
        static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
        LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);

        void OnCreate();
        void OnPaint();
        void OnSize();
        void OnChar(wchar_t ch);
        void OnKeyDown(WPARAM key, LPARAM lParam);
        void OnMouseWheel(short delta);
        void OnLeftButtonDown(int x, int y);
        void OnMouseMove(int x, int y, WPARAM flags);
        void OnLeftButtonUp(int x, int y);
        void OpenProfile(size_t index);
        void EnsureFactories();
        void EnsureRenderTarget();
        void RecomputeMetrics();
        void ResizeTerminalToClient();
        void Invalidate();
        void DrawTabs();
        void DrawTerminal();
        void DrawStatusBar();
        bool IsPointInTerminal(int x, int y) const;
        std::optional<size_t> HitTestTab(int x, int y) const;
        wsh::terminal::SelectionPoint ClientToBufferPoint(int x, int y) const;
        void CopySelection();
        void PasteClipboard();
        void CloseActiveTab();
        void UpdateWindowTitle();
        std::wstring SettingsPath() const;

        HWND hwnd_ = nullptr;
        HINSTANCE instance_ = nullptr;
        Microsoft::WRL::ComPtr<ID2D1Factory> d2dFactory_;
        Microsoft::WRL::ComPtr<IDWriteFactory> dwriteFactory_;
        Microsoft::WRL::ComPtr<ID2D1HwndRenderTarget> renderTarget_;
        Microsoft::WRL::ComPtr<IDWriteTextFormat> terminalFormat_;
        Microsoft::WRL::ComPtr<IDWriteTextFormat> uiFormat_;
        config::Settings settings_{};
        std::optional<wsh::workspace::Workspace> workspace_;

        float charWidth_ = 10.0f;
        float lineHeight_ = 20.0f;
        int terminalColumns_ = 80;
        int terminalRows_ = 24;
        int tabBarHeight_ = 40;
        int statusBarHeight_ = 30;
        int padding_ = 10;

        bool selecting_ = false;
        std::optional<wsh::terminal::SelectionPoint> selectionStart_;
        std::optional<wsh::terminal::SelectionPoint> selectionEnd_;
    };
}
