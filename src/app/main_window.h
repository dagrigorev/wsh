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
        void OnLeftButtonDoubleClick(int x, int y);
        void OnMiddleButtonDown(int x, int y);
        void OnMiddleButtonUp(int x, int y);
        void OnMouseLeave();
        void OpenProfile(size_t index);
        void EnsureFactories();
        void EnsureRenderTarget();
        void RecomputeMetrics();
        void ResizeTerminalToClient();
        void Invalidate();
        void DrawHeader();
        void DrawWindowControls();
        void DrawTabs();
        void DrawTerminal();
        void DrawStatusBar();
        void ShowWorkspaceMenu(int x, int y);
        bool IsPointInTerminal(int x, int y) const;
        bool IsPointInDraggableHeader(int x, int y) const;
        std::optional<int> HitTestWindowControl(int x, int y) const;
        std::optional<size_t> HitTestTab(int x, int y) const;
        std::optional<size_t> HitTestTabClose(int x, int y) const;
        bool IsPointInNewTabButton(int x, int y) const;
        std::optional<size_t> HitTestSidebarSession(int x, int y) const;
        std::optional<int> HitTestSidebarButton(int x, int y) const;
        std::optional<int> HitTestShellToolbarButton(int x, int y) const;
        std::optional<int> HitTestShellTrafficDot(int x, int y) const;
        bool IsPointInSearchBox(int x, int y) const;
        bool IsPointInWorkspacePill(int x, int y) const;
        wsh::terminal::SelectionPoint ClientToBufferPoint(int x, int y) const;
        void SelectWordAt(int x, int y);
        void SelectLineAt(int x, int y);
        void UpdateSelectionForDrag(int x, int y);
        void CopySelection();
        void PasteClipboard();
        void CloseActiveTab();
        void UpdateWindowTitle();
        std::wstring SettingsPath() const;
        std::wstring BuildTabLabel(size_t index) const;
        std::wstring Ellipsize(const std::wstring& text, size_t maxChars) const;
        void ShowProfileMenu(int x, int y);
        bool UpdateHoverState(int x, int y);
        void EnsureMouseTracking();
        void UpdateCursor();

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
        int appHeaderHeight_ = 64;
        int tabBarHeight_ = 58;
        int statusBarHeight_ = 40;
        int padding_ = 14;

        bool selecting_ = false;
        bool mouseTracking_ = false;
        bool hoverNewTabButton_ = false;
        bool hoverTerminal_ = false;
        bool hoverSearchBox_ = false;
        bool hoverWorkspacePill_ = false;
        bool searchFocused_ = false;
        std::wstring searchQuery_;
        std::optional<int> hoveredWindowControl_;
        std::optional<int> pressedWindowControl_;
        std::optional<size_t> hoveredTab_;
        std::optional<size_t> hoveredCloseTab_;
        std::optional<size_t> hoveredSidebarSession_;
        std::optional<int> hoveredSidebarButton_;
        std::optional<int> hoveredShellToolbarButton_;
        std::optional<int> hoveredShellTrafficDot_;
        std::optional<wsh::terminal::SelectionPoint> selectionStart_;
        std::optional<wsh::terminal::SelectionPoint> selectionEnd_;
        DWORD lastDoubleClickTick_ = 0;
        std::optional<wsh::terminal::SelectionPoint> lastDoubleClickPoint_;
    };
}
