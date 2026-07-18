#include "ui.h"

#include "resource.h"
#include "version.h"
#include "win_util.h"

#include <algorithm>
#include <array>
#include <sstream>

namespace agent_latch {
namespace {

constexpr COLORREF kBackground = RGB(11, 14, 18);
constexpr COLORREF kSurface = RGB(20, 24, 30);
constexpr COLORREF kSurfaceRaised = RGB(27, 32, 39);
constexpr COLORREF kBorder = RGB(43, 49, 59);
constexpr COLORREF kText = RGB(244, 247, 250);
constexpr COLORREF kMuted = RGB(157, 166, 178);
constexpr COLORREF kFaint = RGB(103, 113, 128);
constexpr COLORREF kActive = RGB(52, 211, 153);
constexpr COLORREF kActiveDark = RGB(18, 52, 44);
constexpr COLORREF kIdle = RGB(112, 122, 136);
constexpr COLORREF kBlue = RGB(104, 169, 255);
constexpr COLORREF kWarning = RGB(251, 191, 36);

HFONT CreateUiFont(UINT dpi, int points, int weight) {
    return CreateFontW(
        -MulDiv(points, static_cast<int>(dpi), 72),
        0,
        0,
        0,
        weight,
        FALSE,
        FALSE,
        FALSE,
        DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE,
        L"Segoe UI Variable Text");
}

void FillRectangle(HDC dc, const RECT& rectangle, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(dc, &rectangle, brush);
    DeleteObject(brush);
}

void FillRoundedRectangle(HDC dc, const RECT& rectangle, int radius, COLORREF fill, COLORREF border) {
    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, border);
    HGDIOBJ old_brush = SelectObject(dc, brush);
    HGDIOBJ old_pen = SelectObject(dc, pen);
    RoundRect(dc, rectangle.left, rectangle.top, rectangle.right, rectangle.bottom, radius, radius);
    SelectObject(dc, old_pen);
    SelectObject(dc, old_brush);
    DeleteObject(pen);
    DeleteObject(brush);
}

void DrawTextBlock(
    HDC dc,
    const std::wstring& text,
    RECT rectangle,
    HFONT font,
    COLORREF color,
    UINT format = DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX) {
    HGDIOBJ old_font = SelectObject(dc, font);
    SetTextColor(dc, color);
    SetBkMode(dc, TRANSPARENT);
    DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), &rectangle, format);
    SelectObject(dc, old_font);
}

void DrawStatusDot(HDC dc, int center_x, int center_y, int radius, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    HPEN pen = CreatePen(PS_SOLID, 1, color);
    HGDIOBJ old_brush = SelectObject(dc, brush);
    HGDIOBJ old_pen = SelectObject(dc, pen);
    Ellipse(dc, center_x - radius, center_y - radius, center_x + radius, center_y + radius);
    SelectObject(dc, old_pen);
    SelectObject(dc, old_brush);
    DeleteObject(pen);
    DeleteObject(brush);
}

void DrawSwitch(HDC dc, const RECT& rectangle, bool enabled) {
    FillRoundedRectangle(
        dc,
        rectangle,
        (rectangle.bottom - rectangle.top) / 2,
        enabled ? RGB(24, 103, 82) : RGB(49, 56, 67),
        enabled ? RGB(42, 151, 116) : RGB(72, 80, 93));

    const int inset = 3;
    const int diameter = rectangle.bottom - rectangle.top - (inset * 2);
    const int left = enabled ? rectangle.right - inset - diameter : rectangle.left + inset;
    RECT thumb{left, rectangle.top + inset, left + diameter, rectangle.top + inset + diameter};
    HBRUSH brush = CreateSolidBrush(enabled ? kActive : kMuted);
    HPEN pen = CreatePen(PS_SOLID, 1, enabled ? kActive : kMuted);
    HGDIOBJ old_brush = SelectObject(dc, brush);
    HGDIOBJ old_pen = SelectObject(dc, pen);
    Ellipse(dc, thumb.left, thumb.top, thumb.right, thumb.bottom);
    SelectObject(dc, old_pen);
    SelectObject(dc, old_brush);
    DeleteObject(pen);
    DeleteObject(brush);
}

std::wstring LatchSecondaryText(const Latch& latch, ULONGLONG now) {
    if (latch.kind == LatchKind::Detector) {
        if (latch.label.ends_with(L" open")) {
            return L"App open";
        }
        if (latch.instance_count > 1) {
            return std::to_wstring(latch.instance_count) + L" tasks running";
        }
        return L"1 task running";
    }
    return FormatCountdown(latch.expires_at, now);
}

UiAction ProviderAction(Provider provider) {
    switch (provider) {
        case Provider::Codex:
            return UiAction::ToggleCodex;
        case Provider::ClaudeCode:
            return UiAction::ToggleClaude;
        case Provider::Cursor:
            return UiAction::ToggleCursor;
        case Provider::OpenCode:
            return UiAction::ToggleOpenCode;
        case Provider::GeminiCli:
            return UiAction::ToggleGemini;
        case Provider::Manual:
        case Provider::External:
            return UiAction::None;
    }
    return UiAction::None;
}

}  // namespace

COLORREF ProviderColor(Provider provider) {
    switch (provider) {
        case Provider::Manual:
            return kBlue;
        case Provider::Codex:
            return RGB(16, 185, 129);
        case Provider::ClaudeCode:
            return RGB(249, 115, 22);
        case Provider::Cursor:
            return RGB(167, 139, 250);
        case Provider::OpenCode:
            return RGB(45, 212, 191);
        case Provider::GeminiCli:
            return RGB(96, 165, 250);
        case Provider::External:
            return kMuted;
    }
    return kMuted;
}

DashboardRenderer::~DashboardRenderer() {
    ResetFonts();
}

void DashboardRenderer::ResetFonts() {
    const std::array<HFONT*, 6> fonts = {
        &title_font_, &heading_font_, &body_font_, &body_semibold_font_, &small_font_, &tiny_semibold_font_};
    for (HFONT* font : fonts) {
        if (*font != nullptr) {
            DeleteObject(*font);
            *font = nullptr;
        }
    }
    if (brand_icon_ != nullptr) {
        DestroyIcon(brand_icon_);
        brand_icon_ = nullptr;
    }
}

void DashboardRenderer::Initialize(UINT dpi) {
    ResetFonts();
    dpi_ = dpi == 0 ? 96 : dpi;
    title_font_ = CreateUiFont(dpi_, 18, FW_SEMIBOLD);
    heading_font_ = CreateUiFont(dpi_, 13, FW_SEMIBOLD);
    body_font_ = CreateUiFont(dpi_, 10, FW_NORMAL);
    body_semibold_font_ = CreateUiFont(dpi_, 10, FW_SEMIBOLD);
    small_font_ = CreateUiFont(dpi_, 8, FW_NORMAL);
    tiny_semibold_font_ = CreateUiFont(dpi_, 8, FW_SEMIBOLD);
    brand_icon_ = static_cast<HICON>(LoadImageW(
        GetModuleHandleW(nullptr),
        MAKEINTRESOURCEW(IDI_AGENTLATCH),
        IMAGE_ICON,
        Scale(38),
        Scale(38),
        LR_DEFAULTCOLOR));
}

int DashboardRenderer::Scale(int value) const {
    return MulDiv(value, static_cast<int>(dpi_), 96);
}

SIZE DashboardRenderer::PreferredClientSize() const {
    return SIZE{Scale(500), Scale(714)};
}

void DashboardRenderer::AddHitTarget(const RECT& rectangle, UiAction action) {
    hit_targets_.push_back(HitTarget{rectangle, action});
}

UiAction DashboardRenderer::HitTest(POINT point) const {
    for (auto iterator = hit_targets_.rbegin(); iterator != hit_targets_.rend(); ++iterator) {
        if (PtInRect(&iterator->rectangle, point)) {
            return iterator->action;
        }
    }
    return UiAction::None;
}

void DashboardRenderer::Paint(HDC target, const RECT& client, const DashboardState& state) {
    hit_targets_.clear();
    const int width = client.right - client.left;
    const int height = client.bottom - client.top;
    HDC memory = CreateCompatibleDC(target);
    HBITMAP bitmap = CreateCompatibleBitmap(target, std::max(1, width), std::max(1, height));
    HGDIOBJ old_bitmap = SelectObject(memory, bitmap);
    FillRectangle(memory, client, kBackground);

    const int margin = Scale(20);
    const int content_right = width - margin;

    RECT logo{margin, Scale(16), margin + Scale(38), Scale(54)};
    if (brand_icon_ != nullptr) {
        DrawIconEx(memory, logo.left, logo.top, brand_icon_, logo.right - logo.left, logo.bottom - logo.top, 0, nullptr, DI_NORMAL);
    }

    RECT title{logo.right + Scale(12), Scale(13), logo.right + Scale(158), Scale(45)};
    DrawTextBlock(memory, L"AgentLatch", title, title_font_, kText, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
    RECT version_badge{title.right + Scale(5), Scale(21), content_right, Scale(44)};
    DrawTextBlock(
        memory,
        L"v" + std::wstring(kAgentLatchVersion),
        version_badge,
        small_font_,
        kFaint);

    RECT hero{margin, Scale(70), content_right, Scale(143)};
    FillRoundedRectangle(memory, hero, Scale(14), state.active ? RGB(15, 38, 33) : kSurface, state.active ? RGB(31, 91, 72) : kBorder);
    DrawStatusDot(memory, hero.left + Scale(22), hero.top + Scale(25), Scale(5), state.active ? kActive : kIdle);
    RECT hero_label{hero.left + Scale(37), hero.top + Scale(10), hero.right - Scale(94), hero.top + Scale(38)};
    const std::wstring headline = state.active ? L"Keeping your PC awake" : L"Ready when your agents run";
    DrawTextBlock(memory, headline, hero_label, heading_font_, state.active ? kActive : kText);

    RECT hero_detail{hero.left + Scale(22), hero.top + Scale(39), hero.right - Scale(20), hero.bottom - Scale(7)};
    std::wstring detail;
    if (!state.power_request_available) {
        detail = L"Windows rejected the power request.";
    } else if (state.active) {
        detail = std::to_wstring(state.latches.size()) +
                 (state.latches.size() == 1 ? L" latch is active" : L" latches are active");
        detail += state.keep_display_on ? L" · display stays on" : L" · display may sleep";
    } else {
        detail = L"Windows can sleep · watching for active agent work";
    }
    DrawTextBlock(
        memory,
        detail,
        hero_detail,
        body_font_,
        state.power_request_available ? kMuted : kWarning,
        DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);

    RECT state_chip{hero.right - Scale(78), hero.top + Scale(13), hero.right - Scale(14), hero.top + Scale(37)};
    FillRoundedRectangle(memory, state_chip, Scale(12), state.active ? kActiveDark : kSurfaceRaised, state.active ? RGB(31, 91, 72) : kBorder);
    DrawTextBlock(
        memory,
        state.active ? L"ACTIVE" : L"IDLE",
        state_chip,
        tiny_semibold_font_,
        state.active ? kActive : kFaint,
        DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
    RECT active_title{margin, Scale(160), content_right, Scale(183)};
    DrawTextBlock(memory, L"Running now", active_title, body_semibold_font_, kText);

    RECT latch_card{margin, Scale(185), content_right, Scale(265)};
    FillRoundedRectangle(memory, latch_card, Scale(12), kSurface, kBorder);
    if (state.latches.empty()) {
        DrawStatusDot(memory, latch_card.left + Scale(22), latch_card.top + Scale(40), Scale(4), kFaint);
        RECT empty_title{latch_card.left + Scale(36), latch_card.top + Scale(13), latch_card.right - Scale(16), latch_card.top + Scale(39)};
        DrawTextBlock(memory, L"No active tasks", empty_title, body_semibold_font_, kText);
        RECT empty_text{empty_title.left, latch_card.top + Scale(37), latch_card.right - Scale(16), latch_card.bottom - Scale(8)};
        DrawTextBlock(memory, L"Monitoring Codex and your enabled agent tools", empty_text, small_font_, kMuted);
    } else {
        const std::size_t visible_count = std::min<std::size_t>(2, state.latches.size());
        for (std::size_t index = 0; index < visible_count; ++index) {
            const Latch& latch = state.latches[index];
            const int first_top = visible_count == 1 ? latch_card.top + Scale(20) : latch_card.top;
            const int top = first_top + static_cast<int>(index) * Scale(40);
            if (index > 0) {
                RECT divider{latch_card.left + Scale(16), top, latch_card.right - Scale(16), top + 1};
                FillRectangle(memory, divider, kBorder);
            }
            DrawStatusDot(memory, latch_card.left + Scale(22), top + Scale(20), Scale(4), ProviderColor(latch.provider));
            RECT row_title{latch_card.left + Scale(36), top + Scale(5), latch_card.right - Scale(120), top + Scale(35)};
            DrawTextBlock(memory, latch.label, row_title, body_semibold_font_, kText);
            RECT row_detail{latch_card.right - Scale(126), top + Scale(5), latch_card.right - Scale(16), top + Scale(35)};
            DrawTextBlock(memory, LatchSecondaryText(latch, state.now), row_detail, small_font_, kMuted, DT_RIGHT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
        }
        if (state.latches.size() > 2) {
            RECT more{latch_card.right - Scale(90), latch_card.bottom - Scale(21), latch_card.right - Scale(16), latch_card.bottom - Scale(4)};
            DrawTextBlock(memory, L"+" + std::to_wstring(state.latches.size() - 2) + L" more", more, small_font_, kFaint, DT_RIGHT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
        }
    }

    RECT automation_title{margin, Scale(282), content_right, Scale(307)};
    DrawTextBlock(memory, L"Agent tracking", automation_title, body_semibold_font_, kText);
    RECT automation_hint{margin + Scale(132), Scale(282), content_right, Scale(307)};
    DrawTextBlock(memory, L"Click to cycle Tasks · Open · Off", automation_hint, small_font_, kFaint, DT_RIGHT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);

    static constexpr std::array<Provider, 5> providers = {
        Provider::Codex,
        Provider::ClaudeCode,
        Provider::Cursor,
        Provider::OpenCode,
        Provider::GeminiCli,
    };
    RECT provider_list{margin, Scale(309), content_right, Scale(479)};
    FillRoundedRectangle(memory, provider_list, Scale(12), kSurface, kBorder);
    for (std::size_t index = 0; index < providers.size(); ++index) {
        const Provider provider = providers[index];
        const DetectionMode mode = state.settings.ProviderMode(provider);
        const bool enabled = mode != DetectionMode::Off;
        const bool running = std::any_of(state.latches.begin(), state.latches.end(), [provider](const Latch& latch) {
            return latch.provider == provider;
        });
        const COLORREF accent = running ? ProviderColor(provider) : enabled ? kMuted : kFaint;
        const int top = provider_list.top + static_cast<int>(index) * Scale(34);
        RECT provider_row{provider_list.left, top, provider_list.right, top + Scale(34)};
        if (index > 0) {
            RECT divider{provider_list.left + Scale(36), top, provider_list.right - Scale(14), top + 1};
            FillRectangle(memory, divider, kBorder);
        }
        DrawStatusDot(memory, provider_row.left + Scale(18), provider_row.top + Scale(17), Scale(3), accent);
        RECT provider_text{provider_row.left + Scale(31), provider_row.top, provider_row.right - Scale(140), provider_row.bottom};
        DrawTextBlock(memory, ProviderShortName(provider), provider_text, small_font_, enabled ? kText : kFaint);
        if (running) {
            RECT running_text{provider_row.right - Scale(136), provider_row.top, provider_row.right - Scale(70), provider_row.bottom};
            DrawTextBlock(memory, L"RUNNING", running_text, tiny_semibold_font_, ProviderColor(provider), DT_RIGHT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
        }
        RECT mode_pill{provider_row.right - Scale(62), provider_row.top + Scale(7), provider_row.right - Scale(12), provider_row.bottom - Scale(7)};
        FillRoundedRectangle(memory, mode_pill, Scale(10), enabled ? kSurfaceRaised : kBackground, kBorder);
        DrawTextBlock(memory, DetectionModeLabel(mode), mode_pill, tiny_semibold_font_, enabled ? kMuted : kFaint, DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
        AddHitTarget(provider_row, ProviderAction(provider));
    }

    RECT settings_title{margin, Scale(496), content_right, Scale(521)};
    DrawTextBlock(memory, L"Settings", settings_title, body_semibold_font_, kText);
    RECT settings_hint{margin + Scale(120), Scale(496), content_right, Scale(521)};
    DrawTextBlock(
        memory,
        L"Click any row to change",
        settings_hint,
        small_font_,
        kFaint,
        DT_RIGHT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);

    RECT settings_card{margin, Scale(523), content_right, Scale(694)};
    FillRoundedRectangle(memory, settings_card, Scale(12), kSurface, kBorder);

    struct SettingRow {
        const wchar_t* title;
        const wchar_t* description;
        bool enabled;
        UiAction action;
    };
    const std::array<SettingRow, 3> rows = {{
        {
            L"Keep screen on while agents work",
            L"Off still keeps the PC awake; only the screen may turn off.",
            state.keep_display_on,
            UiAction::ToggleDisplay,
        },
        {
            L"Launch AgentLatch at sign-in",
            L"Starts monitoring automatically when you sign in to Windows.",
            state.start_with_windows,
            UiAction::ToggleStartup,
        },
        {
            L"Show wake status notifications",
            L"Alerts you when AgentLatch starts or stops keeping the PC awake.",
            state.settings.notifications,
            UiAction::ToggleNotifications,
        },
    }};

    for (std::size_t index = 0; index < rows.size(); ++index) {
        const int top = settings_card.top + static_cast<int>(index) * Scale(57);
        RECT row{settings_card.left, top, settings_card.right, top + Scale(57)};
        if (index > 0) {
            RECT divider{row.left + Scale(16), row.top, row.right - Scale(16), row.top + 1};
            FillRectangle(memory, divider, kBorder);
        }
        RECT row_title{row.left + Scale(16), row.top + Scale(5), row.right - Scale(112), row.top + Scale(30)};
        DrawTextBlock(memory, rows[index].title, row_title, small_font_, kText);
        RECT row_description{row.left + Scale(16), row.top + Scale(26), row.right - Scale(112), row.bottom - Scale(4)};
        DrawTextBlock(
            memory,
            rows[index].description,
            row_description,
            small_font_,
            kMuted,
            DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);

        RECT state_text{row.right - Scale(105), row.top, row.right - Scale(60), row.bottom};
        DrawTextBlock(
            memory,
            rows[index].enabled ? L"On" : L"Off",
            state_text,
            tiny_semibold_font_,
            rows[index].enabled ? kActive : kMuted,
            DT_RIGHT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
        RECT toggle{row.right - Scale(50), row.top + Scale(18), row.right - Scale(14), row.top + Scale(39)};
        DrawSwitch(memory, toggle, rows[index].enabled);
        AddHitTarget(row, rows[index].action);
    }

    BitBlt(target, 0, 0, width, height, memory, 0, 0, SRCCOPY);
    SelectObject(memory, old_bitmap);
    DeleteObject(bitmap);
    DeleteDC(memory);
}

}  // namespace agent_latch
