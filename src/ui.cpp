#include "ui.h"

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
constexpr COLORREF kBlueDark = RGB(27, 48, 75);
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

void DrawLatchMark(HDC dc, const RECT& bounds, COLORREF color, COLORREF background) {
    const int width = bounds.right - bounds.left;
    const int stroke = std::max(2, width / 11);
    HPEN pen = CreatePen(PS_SOLID, stroke, color);
    HBRUSH fill = CreateSolidBrush(color);
    HGDIOBJ old_pen = SelectObject(dc, pen);
    HGDIOBJ old_brush = SelectObject(dc, GetStockObject(NULL_BRUSH));

    const int shackle_left = bounds.left + width * 3 / 10;
    const int shackle_right = bounds.right - width * 3 / 10;
    const int shackle_top = bounds.top + width / 7;
    const int shackle_bottom = bounds.top + width * 3 / 5;
    Arc(dc, shackle_left, shackle_top, shackle_right, shackle_bottom, shackle_right, shackle_bottom, shackle_left, shackle_bottom);
    MoveToEx(dc, shackle_left, (shackle_top + shackle_bottom) / 2, nullptr);
    LineTo(dc, shackle_left, shackle_bottom);
    MoveToEx(dc, shackle_right, (shackle_top + shackle_bottom) / 2, nullptr);
    LineTo(dc, shackle_right, shackle_bottom);

    SelectObject(dc, fill);
    RECT body{bounds.left + width / 5, bounds.top + width / 2, bounds.right - width / 5, bounds.bottom - width / 7};
    RoundRect(dc, body.left, body.top, body.right, body.bottom, stroke * 2, stroke * 2);

    HBRUSH key_brush = CreateSolidBrush(background);
    SelectObject(dc, key_brush);
    const int key_radius = std::max(1, width / 18);
    const int key_x = (body.left + body.right) / 2;
    const int key_y = (body.top + body.bottom) / 2;
    Ellipse(dc, key_x - key_radius, key_y - key_radius, key_x + key_radius, key_y + key_radius);

    SelectObject(dc, old_brush);
    SelectObject(dc, old_pen);
    DeleteObject(key_brush);
    DeleteObject(fill);
    DeleteObject(pen);
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
}

int DashboardRenderer::Scale(int value) const {
    return MulDiv(value, static_cast<int>(dpi_), 96);
}

SIZE DashboardRenderer::PreferredClientSize() const {
    return SIZE{Scale(476), Scale(638)};
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

    RECT logo{margin, Scale(17), margin + Scale(36), Scale(53)};
    FillRoundedRectangle(memory, logo, Scale(11), state.active ? kActiveDark : kSurfaceRaised, state.active ? RGB(38, 113, 89) : kBorder);
    RECT logo_mark{logo.left + Scale(7), logo.top + Scale(6), logo.right - Scale(7), logo.bottom - Scale(6)};
    DrawLatchMark(memory, logo_mark, state.active ? kActive : kMuted, state.active ? kActiveDark : kSurfaceRaised);

    RECT title{logo.right + Scale(11), Scale(14), logo.right + Scale(130), Scale(43)};
    DrawTextBlock(memory, L"AgentLatch", title, title_font_, kText);
    RECT version_badge{title.right + Scale(7), Scale(22), title.right + Scale(119), Scale(43)};
    DrawTextBlock(
        memory,
        L"v" + std::wstring(kAgentLatchVersion),
        version_badge,
        small_font_,
        kFaint);

    RECT hooks_button{content_right - Scale(96), Scale(20), content_right, Scale(50)};
    FillRoundedRectangle(memory, hooks_button, Scale(10), kSurface, kBorder);
    DrawTextBlock(
        memory,
        L"Integrations",
        hooks_button,
        tiny_semibold_font_,
        kBlue,
        DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
    AddHitTarget(hooks_button, UiAction::SetupHooks);

    RECT hero{margin, Scale(70), content_right, Scale(138)};
    FillRoundedRectangle(memory, hero, Scale(14), state.active ? RGB(15, 38, 33) : kSurface, state.active ? RGB(31, 91, 72) : kBorder);
    DrawStatusDot(memory, hero.left + Scale(22), hero.top + Scale(25), Scale(5), state.active ? kActive : kIdle);
    RECT hero_label{hero.left + Scale(37), hero.top + Scale(10), hero.right - Scale(94), hero.top + Scale(38)};
    const std::wstring headline = state.active ? L"Keeping your PC awake" : L"Ready when your agents run";
    DrawTextBlock(memory, headline, hero_label, heading_font_, state.active ? kActive : kText);

    RECT hero_detail{hero.left + Scale(22), hero.top + Scale(38), hero.right - Scale(20), hero.bottom - Scale(7)};
    std::wstring detail;
    if (!state.power_request_available) {
        detail = L"Windows rejected the power request.";
    } else if (state.active) {
        detail = std::to_wstring(state.latches.size()) +
                 (state.latches.size() == 1 ? L" latch is active" : L" latches are active");
        detail += state.keep_display_on ? L" · display stays on" : L" · display may sleep";
    } else {
        detail = L"Windows can sleep · active tasks are monitored automatically";
    }
    DrawTextBlock(memory, detail, hero_detail, body_font_, state.power_request_available ? kMuted : kWarning);

    const bool has_manual = std::any_of(state.latches.begin(), state.latches.end(), [](const Latch& latch) {
        return latch.kind == LatchKind::Manual || latch.kind == LatchKind::Timer;
    });
    RECT state_chip{hero.right - Scale(78), hero.top + Scale(13), hero.right - Scale(14), hero.top + Scale(37)};
    FillRoundedRectangle(memory, state_chip, Scale(12), state.active ? kActiveDark : kSurfaceRaised, state.active ? RGB(31, 91, 72) : kBorder);
    DrawTextBlock(
        memory,
        state.active ? L"ACTIVE" : L"IDLE",
        state_chip,
        tiny_semibold_font_,
        state.active ? kActive : kFaint,
        DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
    if (has_manual) {
        RECT release{hero.right - Scale(94), hero.bottom - Scale(27), hero.right - Scale(14), hero.bottom - Scale(7)};
        DrawTextBlock(memory, L"Release", release, small_font_, kBlue, DT_RIGHT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
        AddHitTarget(release, UiAction::ReleaseManual);
    }

    RECT quick_title{margin, Scale(153), content_right, Scale(176)};
    DrawTextBlock(memory, L"Keep awake for", quick_title, body_semibold_font_, kText);

    const std::array<std::pair<const wchar_t*, UiAction>, 4> quick_actions = {{
        {L"30m", UiAction::Timer30Minutes},
        {L"1 hour", UiAction::Timer1Hour},
        {L"2 hours", UiAction::Timer2Hours},
        {L"Until stopped", UiAction::ManualUntilReleased},
    }};
    const int gap = Scale(6);
    const int available_width = content_right - margin - gap * 3;
    const int button_width = available_width / 4;
    for (std::size_t index = 0; index < quick_actions.size(); ++index) {
        const int left = margin + static_cast<int>(index) * (button_width + gap);
        RECT button{left, Scale(178), left + button_width, Scale(216)};
        FillRoundedRectangle(memory, button, Scale(10), index == 3 ? kBlueDark : kSurface, index == 3 ? RGB(51, 85, 126) : kBorder);
        DrawTextBlock(memory, quick_actions[index].first, button, small_font_, index == 3 ? kBlue : kText, DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
        AddHitTarget(button, quick_actions[index].second);
    }

    RECT active_title{margin, Scale(233), content_right, Scale(256)};
    DrawTextBlock(memory, L"Running now", active_title, body_semibold_font_, kText);

    RECT latch_card{margin, Scale(258), content_right, Scale(338)};
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

    RECT automation_title{margin, Scale(355), content_right, Scale(380)};
    DrawTextBlock(memory, L"Agent tracking", automation_title, body_semibold_font_, kText);
    RECT automation_hint{margin + Scale(132), Scale(355), content_right, Scale(380)};
    DrawTextBlock(memory, L"Click a row to change its mode", automation_hint, small_font_, kFaint, DT_RIGHT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);

    static constexpr std::array<Provider, 5> providers = {
        Provider::Codex,
        Provider::ClaudeCode,
        Provider::Cursor,
        Provider::OpenCode,
        Provider::GeminiCli,
    };
    RECT provider_list{margin, Scale(382), content_right, Scale(552)};
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

    RECT footer{margin, Scale(570), content_right, Scale(616)};
    FillRoundedRectangle(memory, footer, Scale(12), kSurface, kBorder);
    const int half = (footer.right - footer.left) / 2;
    RECT display_area{footer.left, footer.top, footer.left + half, footer.bottom};
    RECT startup_area{footer.left + half, footer.top, footer.right, footer.bottom};
    RECT footer_divider{footer.left + half, footer.top + Scale(9), footer.left + half + 1, footer.bottom - Scale(9)};
    FillRectangle(memory, footer_divider, kBorder);
    DrawStatusDot(memory, display_area.left + Scale(18), display_area.top + Scale(23), Scale(4), state.keep_display_on ? kBlue : kFaint);
    RECT display_text{display_area.left + Scale(30), display_area.top, display_area.right - Scale(8), display_area.bottom};
    DrawTextBlock(memory, state.keep_display_on ? L"Display on" : L"Display may sleep", display_text, small_font_, state.keep_display_on ? kText : kMuted);
    DrawStatusDot(memory, startup_area.left + Scale(18), startup_area.top + Scale(23), Scale(4), state.start_with_windows ? kActive : kFaint);
    RECT startup_text{startup_area.left + Scale(30), startup_area.top, startup_area.right - Scale(8), startup_area.bottom};
    DrawTextBlock(memory, state.start_with_windows ? L"Starts with Windows" : L"Start at sign-in", startup_text, small_font_, state.start_with_windows ? kText : kMuted);
    AddHitTarget(display_area, UiAction::ToggleDisplay);
    AddHitTarget(startup_area, UiAction::ToggleStartup);

    BitBlt(target, 0, 0, width, height, memory, 0, 0, SRCCOPY);
    SelectObject(memory, old_bitmap);
    DeleteObject(bitmap);
    DeleteDC(memory);
}

}  // namespace agent_latch
