#include "ui.h"
#include "version.h"
#include <algorithm>

namespace agent_latch {

bool WindowsHighContrast() {
    HIGHCONTRASTW value{}; value.cbSize = sizeof(value);
    return SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(value), &value, 0) && (value.dwFlags & HCF_HIGHCONTRASTON);
}
bool WindowsAppsUseDarkMode() {
    DWORD light = 1, size = sizeof(light);
    return RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &light, &size) == ERROR_SUCCESS && light == 0;
}
DashboardPalette ResolveDashboardPalette(Appearance appearance, bool high_contrast) {
    if (high_contrast) {
        return {false, GetSysColor(COLOR_WINDOW), GetSysColor(COLOR_WINDOW), GetSysColor(COLOR_BTNFACE),
            GetSysColor(COLOR_WINDOWTEXT), GetSysColor(COLOR_WINDOWTEXT), GetSysColor(COLOR_WINDOWTEXT),
            GetSysColor(COLOR_HIGHLIGHT), GetSysColor(COLOR_HOTLIGHT), GetSysColor(COLOR_WINDOWTEXT)};
    }
    if (appearance == Appearance::Dark || (appearance == Appearance::System && WindowsAppsUseDarkMode())) {
        return {true, RGB(24, 26, 30), RGB(30, 33, 38), RGB(43, 47, 54), RGB(240, 242, 246),
            RGB(175, 183, 195), RGB(77, 84, 96), RGB(37, 77, 110), RGB(81, 218, 165), RGB(255, 198, 92)};
    }
    return {false, GetSysColor(COLOR_WINDOW), GetSysColor(COLOR_WINDOW), GetSysColor(COLOR_BTNFACE),
        GetSysColor(COLOR_WINDOWTEXT), RGB(96, 106, 118), GetSysColor(COLOR_3DSHADOW),
        GetSysColor(COLOR_HIGHLIGHT), RGB(14, 109, 72), RGB(151, 82, 0)};
}

namespace {
void Fill(HDC dc, RECT rect, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color); FillRect(dc, &rect, brush); DeleteObject(brush);
}
void Border(HDC dc, RECT rect, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color); FrameRect(dc, &rect, brush); DeleteObject(brush);
}
void DrawLabel(HDC dc, RECT rect, const wchar_t* text, COLORREF color, UINT flags = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX) {
    SetBkMode(dc, TRANSPARENT); SetTextColor(dc, color); DrawTextW(dc, text, -1, &rect, flags);
}
}

void DashboardControls::PaintDarkControl(HWND window, HDC dc) const {
    RECT bounds{}; GetClientRect(window, &bounds);
    const int id = GetDlgCtrlID(window);
    HGDIOBJ old_font = SelectObject(dc, body_font_);
    Fill(dc, bounds, palette_.page);
    if (window == tabs_) {
        RECT pane = bounds; TabCtrl_AdjustRect(window, FALSE, &pane);
        Border(dc, pane, palette_.border);
        for (int index = 0; index < TabCtrl_GetItemCount(window); ++index) {
            RECT rect{}; TabCtrl_GetItemRect(window, index, &rect);
            const bool selected = index == TabCtrl_GetCurSel(window);
            Fill(dc, rect, selected ? palette_.surface : palette_.background);
            Border(dc, rect, palette_.border);
            wchar_t label[64]{}; TCITEMW item{}; item.mask = TCIF_TEXT; item.pszText = label; item.cchTextMax = 64;
            TabCtrl_GetItem(window, index, &item);
            DrawLabel(dc, rect, label, selected ? palette_.text : palette_.muted, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            if (selected && GetFocus() == window) { InflateRect(&rect, -Scale(4), -Scale(4)); DrawFocusRect(dc, &rect); }
        }
    } else if (window == footer_) {
        Fill(dc, bounds, palette_.background);
        RECT line{0, 0, bounds.right, 1}; Fill(dc, line, palette_.border);
        for (int index = 0; index < 2; ++index) {
            RECT part{}; SendMessageW(window, SB_GETRECT, static_cast<WPARAM>(index), reinterpret_cast<LPARAM>(&part));
            wchar_t label[256]{}; SendMessageW(window, SB_GETTEXTW, static_cast<WPARAM>(index), reinterpret_cast<LPARAM>(label));
            part.left += Scale(7); part.right -= Scale(10);
            DrawLabel(dc, part, label, palette_.muted, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
        }
        HPEN pen = CreatePen(PS_SOLID, 1, palette_.muted); HGDIOBJ old = SelectObject(dc, pen);
        for (int offset : {4, 8, 12}) {
            MoveToEx(dc, bounds.right - Scale(offset), bounds.bottom - Scale(2), nullptr);
            LineTo(dc, bounds.right - Scale(2), bounds.bottom - Scale(offset));
        }
        SelectObject(dc, old); DeleteObject(pen);
    } else if (window == ListView_GetHeader(reasons_)) {
        Fill(dc, bounds, palette_.surface);
        for (int index = 0; index < Header_GetItemCount(window); ++index) {
            RECT rect{}; Header_GetItemRect(window, index, &rect);
            Border(dc, rect, palette_.border); rect.left += Scale(6); rect.right -= Scale(6);
            wchar_t label[128]{}; HDITEMW item{}; item.mask = HDI_TEXT; item.pszText = label; item.cchTextMax = 128;
            Header_GetItem(window, index, &item); DrawLabel(dc, rect, label, palette_.text);
        }
    } else if ((id >= 300 && id <= 304) || id == 450 || id == 75) {
        Fill(dc, bounds, palette_.surface);
        Border(dc, bounds, GetFocus() == window ? palette_.accent : palette_.border);
        COMBOBOXINFO info{}; info.cbSize = sizeof(info); GetComboBoxInfo(window, &info);
        wchar_t label[128]{}; GetWindowTextW(window, label, 128);
        RECT text = info.rcItem; text.left += Scale(3);
        DrawLabel(dc, text, label, IsWindowEnabled(window) ? palette_.text : palette_.muted);
        const int x = (info.rcButton.left + info.rcButton.right) / 2, y = (info.rcButton.top + info.rcButton.bottom) / 2;
        HPEN pen = CreatePen(PS_SOLID, 1, palette_.text); HGDIOBJ old = SelectObject(dc, pen);
        MoveToEx(dc, x - Scale(3), y - Scale(1), nullptr); LineTo(dc, x, y + Scale(2)); LineTo(dc, x + Scale(3), y - Scale(1));
        SelectObject(dc, old); DeleteObject(pen);
    } else {
        wchar_t label[256]{}; GetWindowTextW(window, label, 256);
        const auto style = GetWindowLongPtrW(window, GWL_STYLE) & BS_TYPEMASK;
        const auto state = SendMessageW(window, BM_GETSTATE, 0, 0);
        if (style == BS_GROUPBOX) {
            RECT frame = bounds; frame.top += Scale(8); Border(dc, frame, palette_.border);
            SIZE extent{}; GetTextExtentPoint32W(dc, label, lstrlenW(label), &extent);
            RECT title{Scale(10), 0, Scale(18) + extent.cx, Scale(22)}; Fill(dc, title, palette_.page);
            title.left += Scale(3); DrawLabel(dc, title, label, palette_.text);
        } else if (style == BS_AUTOCHECKBOX) {
            const int side = Scale(14), top = (bounds.bottom - side) / 2;
            RECT box{0, top, side, top + side};
            const bool checked = SendMessageW(window, BM_GETCHECK, 0, 0) == BST_CHECKED;
            Fill(dc, box, checked ? palette_.selected : palette_.surface); Border(dc, box, palette_.muted);
            if (checked) {
                HPEN pen = CreatePen(PS_SOLID, std::max(1, Scale(2)), palette_.text); HGDIOBJ old = SelectObject(dc, pen);
                MoveToEx(dc, Scale(3), top + Scale(7), nullptr); LineTo(dc, Scale(6), top + Scale(10)); LineTo(dc, Scale(11), top + Scale(4));
                SelectObject(dc, old); DeleteObject(pen);
            }
            RECT text = bounds; text.left += Scale(20); DrawLabel(dc, text, label, palette_.text);
            if (state & BST_FOCUS) { DrawFocusRect(dc, &text); }
        } else {
            const bool pressed = (state & BST_PUSHED) != 0;
            const bool hot = IsWindowEnabled(window) && GetPropW(window, L"AgentLatch.Hot") != nullptr;
            Fill(dc, bounds, pressed ? palette_.selected : hot ? RGB(55, 61, 70) : palette_.surface);
            Border(dc, bounds, state & BST_FOCUS ? palette_.accent : palette_.border);
            DrawLabel(dc, bounds, label, IsWindowEnabled(window) ? palette_.text : palette_.muted,
                DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            if (state & BST_FOCUS) { RECT focus = bounds; InflateRect(&focus, -Scale(4), -Scale(4)); DrawFocusRect(dc, &focus); }
        }
    }
    SelectObject(dc, old_font);
}

void DashboardControls::PaintDarkScrollbars(HWND window, HDC dc) const {
    RECT client{}; GetClientRect(window, &client);
    RECT outer{}; GetWindowRect(window, &outer);
    RECT border{0, 0, outer.right - outer.left, outer.bottom - outer.top};
    if (GetWindowLongPtrW(window, GWL_STYLE) & WS_BORDER) { Border(dc, border, palette_.border); }
    if (!(GetWindowLongPtrW(window, GWL_STYLE) & WS_VSCROLL)) { return; }
    SCROLLBARINFO info{}; info.cbSize = sizeof(info);
    if (!GetScrollBarInfo(window, OBJID_VSCROLL, &info)) { return; }
    RECT bar = info.rcScrollBar; OffsetRect(&bar, -outer.left, -outer.top);
    Fill(dc, bar, palette_.background);
    RECT thumb{bar.left + 3, bar.top + info.xyThumbTop, bar.right - 3, bar.top + info.xyThumbBottom};
    if (thumb.bottom > thumb.top) { Fill(dc, thumb, palette_.border); }
    const int x = (bar.left + bar.right) / 2;
    HPEN pen = CreatePen(PS_SOLID, 1, palette_.muted); HGDIOBJ old = SelectObject(dc, pen);
    for (int end = 0; end < 2; ++end) {
        const int y = end == 0 ? bar.top + info.dxyLineButton / 2 : bar.bottom - info.dxyLineButton / 2;
        const int direction = end == 0 ? -1 : 1;
        MoveToEx(dc, x - 3, y - direction, nullptr); LineTo(dc, x, y + direction * 2); LineTo(dc, x + 3, y - direction);
    }
    SelectObject(dc, old); DeleteObject(pen);
}

LRESULT CALLBACK DashboardControls::ThemeProcedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam,
                                                  UINT_PTR, DWORD_PTR reference) {
    auto* controls = reinterpret_cast<DashboardControls*>(reference);
    if (window == controls->reasons_ && (message == WM_LBUTTONDOWN || message == WM_LBUTTONDBLCLK ||
        message == WM_RBUTTONDOWN || message == WM_RBUTTONDBLCLK)) {
        return 0; // Read-only status, not an interactive selection box. Scrolling is still native.
    }
    if (message == WM_MOUSEMOVE) {
        SetPropW(window, L"AgentLatch.Hot", reinterpret_cast<HANDLE>(1));
        TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE, window, 0}; TrackMouseEvent(&tracking);
    } else if (message == WM_MOUSELEAVE || message == WM_NCDESTROY) { RemovePropW(window, L"AgentLatch.Hot"); }
    if (controls->palette_.dark) {
        if (window == controls->reasons_) {
            const LRESULT result = DefSubclassProc(window, message, wparam, lparam);
            if (message == WM_NCPAINT) {
                HDC dc = GetWindowDC(window); controls->PaintDarkScrollbars(window, dc); ReleaseDC(window, dc);
            } else if (message == WM_PRINT) { controls->PaintDarkScrollbars(window, reinterpret_cast<HDC>(wparam)); }
            return result;
        }
        if (message == WM_PAINT) {
            PAINTSTRUCT paint{}; HDC dc = BeginPaint(window, &paint); controls->PaintDarkControl(window, dc); EndPaint(window, &paint); return 0;
        }
        if (message == WM_PRINT || message == WM_PRINTCLIENT) {
            controls->PaintDarkControl(window, reinterpret_cast<HDC>(wparam)); return 0;
        }
        if (message == WM_ERASEBKGND) { return 1; }
    }
    const LRESULT result = DefSubclassProc(window, message, wparam, lparam);
    if (message == WM_SETFOCUS || message == WM_KILLFOCUS || message == WM_ENABLE || message == BM_SETCHECK ||
        message == BM_SETSTATE || message == WM_MOUSEMOVE || message == WM_MOUSELEAVE || message == CB_SETCURSEL) {
        if (controls->palette_.dark) { InvalidateRect(window, nullptr, FALSE); }
    }
    return result;
}

}  // namespace agent_latch
