#include "ui.h"

#include "win_util.h"

#include <algorithm>
#include <array>
#include <sstream>

namespace agent_latch {
namespace {

constexpr COLORREF kBackground = RGB(10, 15, 24);
constexpr COLORREF kSurface = RGB(20, 27, 39);
constexpr COLORREF kSurfaceRaised = RGB(28, 37, 51);
constexpr COLORREF kBorder = RGB(45, 57, 75);
constexpr COLORREF kText = RGB(241, 245, 249);
constexpr COLORREF kMuted = RGB(148, 163, 184);
constexpr COLORREF kFaint = RGB(100, 116, 139);
constexpr COLORREF kActive = RGB(52, 211, 153);
constexpr COLORREF kActiveDark = RGB(18, 78, 63);
constexpr COLORREF kIdle = RGB(100, 116, 139);
constexpr COLORREF kBlue = RGB(96, 165, 250);
constexpr COLORREF kBlueDark = RGB(27, 57, 96);
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
    std::wstring text = latch.detail;
    if (!text.empty()) {
        text += L"  ·  ";
    }
    if (latch.kind == LatchKind::Detector) {
        text += L"activity detected";
    } else {
        text += FormatCountdown(latch.expires_at, now);
    }
    return text;
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
    title_font_ = CreateUiFont(dpi_, 22, FW_SEMIBOLD);
    heading_font_ = CreateUiFont(dpi_, 16, FW_SEMIBOLD);
    body_font_ = CreateUiFont(dpi_, 10, FW_NORMAL);
    body_semibold_font_ = CreateUiFont(dpi_, 10, FW_SEMIBOLD);
    small_font_ = CreateUiFont(dpi_, 9, FW_NORMAL);
    tiny_semibold_font_ = CreateUiFont(dpi_, 8, FW_SEMIBOLD);
}

int DashboardRenderer::Scale(int value) const {
    return MulDiv(value, static_cast<int>(dpi_), 96);
}

SIZE DashboardRenderer::PreferredClientSize() const {
    return SIZE{Scale(560), Scale(830)};
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

    const int margin = Scale(24);
    const int content_right = width - margin;

    RECT logo{margin, Scale(24), margin + Scale(42), Scale(66)};
    FillRoundedRectangle(memory, logo, Scale(13), state.active ? kActiveDark : kSurfaceRaised, state.active ? kActive : kBorder);
    RECT logo_mark{logo.left + Scale(7), logo.top + Scale(6), logo.right - Scale(7), logo.bottom - Scale(6)};
    DrawLatchMark(memory, logo_mark, state.active ? kActive : kMuted, state.active ? kActiveDark : kSurfaceRaised);

    RECT title{logo.right + Scale(13), Scale(21), content_right - Scale(110), Scale(52)};
    DrawTextBlock(memory, L"AgentLatch", title, title_font_, kText);
    RECT tagline{title.left, Scale(50), content_right, Scale(72)};
    DrawTextBlock(memory, L"Your agents run. Your PC stays awake.", tagline, small_font_, kMuted);

    RECT hooks_button{content_right - Scale(104), Scale(29), content_right, Scale(61)};
    FillRoundedRectangle(memory, hooks_button, Scale(16), kSurface, kBorder);
    DrawTextBlock(memory, L"SET UP HOOKS", hooks_button, tiny_semibold_font_, kBlue, DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
    AddHitTarget(hooks_button, UiAction::SetupHooks);

    RECT hero{margin, Scale(94), content_right, Scale(224)};
    FillRoundedRectangle(memory, hero, Scale(18), state.active ? RGB(14, 48, 43) : kSurface, state.active ? RGB(30, 115, 91) : kBorder);
    DrawStatusDot(memory, hero.left + Scale(27), hero.top + Scale(31), Scale(6), state.active ? kActive : kIdle);
    RECT hero_label{hero.left + Scale(43), hero.top + Scale(16), hero.right - Scale(20), hero.top + Scale(47)};
    const std::wstring headline = state.active
                                      ? (L"Latched · " + std::to_wstring(state.latches.size()) +
                                         (state.latches.size() == 1 ? L" active latch" : L" active latches"))
                                      : L"Windows can sleep";
    DrawTextBlock(memory, headline, hero_label, heading_font_, state.active ? kActive : kText);

    RECT hero_detail{hero.left + Scale(24), hero.top + Scale(56), hero.right - Scale(22), hero.top + Scale(83)};
    std::wstring detail;
    if (!state.power_request_available) {
        detail = L"Windows rejected the power request. Open diagnostics for details.";
    } else if (state.active) {
        detail = state.keep_display_on ? L"System and display sleep are blocked." : L"System sleep is blocked. The display may turn off normally.";
    } else {
        detail = L"AgentLatch will engage when work begins or you start a manual timer.";
    }
    DrawTextBlock(memory, detail, hero_detail, body_font_, state.power_request_available ? kMuted : kWarning);

    const bool has_manual = std::any_of(state.latches.begin(), state.latches.end(), [](const Latch& latch) {
        return latch.kind == LatchKind::Manual || latch.kind == LatchKind::Timer;
    });
    RECT state_chip{hero.left + Scale(24), hero.bottom - Scale(38), hero.left + Scale(174), hero.bottom - Scale(14)};
    FillRoundedRectangle(memory, state_chip, Scale(12), state.active ? kActiveDark : kSurfaceRaised, state.active ? RGB(30, 115, 91) : kBorder);
    DrawTextBlock(
        memory,
        state.active ? L"POWER REQUEST ACTIVE" : L"POWER REQUEST IDLE",
        state_chip,
        tiny_semibold_font_,
        state.active ? kActive : kFaint,
        DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
    if (has_manual) {
        RECT release{hero.right - Scale(132), hero.bottom - Scale(43), hero.right - Scale(20), hero.bottom - Scale(10)};
        FillRoundedRectangle(memory, release, Scale(16), kSurfaceRaised, kBorder);
        DrawTextBlock(memory, L"Release manual", release, small_font_, kText, DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
        AddHitTarget(release, UiAction::ReleaseManual);
    }

    RECT quick_title{margin, Scale(248), content_right, Scale(274)};
    DrawTextBlock(memory, L"Quick latch", quick_title, body_semibold_font_, kText);
    RECT quick_subtitle{margin + Scale(88), Scale(248), content_right, Scale(274)};
    DrawTextBlock(memory, L"Start a manual keep-awake session", quick_subtitle, small_font_, kFaint);

    const std::array<std::pair<const wchar_t*, UiAction>, 4> quick_actions = {{
        {L"30 min", UiAction::Timer30Minutes},
        {L"1 hour", UiAction::Timer1Hour},
        {L"2 hours", UiAction::Timer2Hours},
        {L"Until released", UiAction::ManualUntilReleased},
    }};
    const int gap = Scale(8);
    const int available_width = content_right - margin - gap * 3;
    const int button_width = available_width / 4;
    for (std::size_t index = 0; index < quick_actions.size(); ++index) {
        const int left = margin + static_cast<int>(index) * (button_width + gap);
        RECT button{left, Scale(282), left + button_width, Scale(330)};
        FillRoundedRectangle(memory, button, Scale(14), index == 3 ? kBlueDark : kSurface, index == 3 ? RGB(51, 91, 145) : kBorder);
        DrawTextBlock(memory, quick_actions[index].first, button, body_semibold_font_, index == 3 ? kBlue : kText, DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
        AddHitTarget(button, quick_actions[index].second);
    }

    RECT active_title{margin, Scale(360), content_right, Scale(388)};
    DrawTextBlock(memory, L"Active work", active_title, body_semibold_font_, kText);

    RECT latch_card{margin, Scale(394), content_right, Scale(588)};
    FillRoundedRectangle(memory, latch_card, Scale(16), kSurface, kBorder);
    if (state.latches.empty()) {
        RECT empty_icon{latch_card.left + Scale(22), latch_card.top + Scale(22), latch_card.left + Scale(60), latch_card.top + Scale(60)};
        FillRoundedRectangle(memory, empty_icon, Scale(12), kSurfaceRaised, kBorder);
        RECT empty_mark{empty_icon.left + Scale(7), empty_icon.top + Scale(6), empty_icon.right - Scale(7), empty_icon.bottom - Scale(6)};
        DrawLatchMark(memory, empty_mark, kFaint, kSurfaceRaised);
        RECT empty_title{empty_icon.right + Scale(14), latch_card.top + Scale(18), latch_card.right - Scale(18), latch_card.top + Scale(48)};
        DrawTextBlock(memory, L"Nothing is holding a latch", empty_title, body_semibold_font_, kText);
        RECT empty_text{empty_title.left, latch_card.top + Scale(45), latch_card.right - Scale(20), latch_card.top + Scale(78)};
        DrawTextBlock(memory, L"Start an agent task or choose a timer above.", empty_text, small_font_, kMuted);
    } else {
        const std::size_t visible_count = std::min<std::size_t>(3, state.latches.size());
        for (std::size_t index = 0; index < visible_count; ++index) {
            const Latch& latch = state.latches[index];
            const int top = latch_card.top + Scale(9) + static_cast<int>(index) * Scale(58);
            if (index > 0) {
                RECT divider{latch_card.left + Scale(20), top - Scale(3), latch_card.right - Scale(20), top - Scale(2)};
                FillRectangle(memory, divider, kBorder);
            }
            DrawStatusDot(memory, latch_card.left + Scale(26), top + Scale(24), Scale(5), ProviderColor(latch.provider));
            RECT row_title{latch_card.left + Scale(44), top + Scale(5), latch_card.right - Scale(78), top + Scale(29)};
            DrawTextBlock(memory, latch.label, row_title, body_semibold_font_, kText);
            RECT row_detail{row_title.left, top + Scale(28), latch_card.right - Scale(18), top + Scale(49)};
            DrawTextBlock(memory, LatchSecondaryText(latch, state.now), row_detail, small_font_, kMuted);
            if (latch.instance_count > 1) {
                RECT count_badge{latch_card.right - Scale(62), top + Scale(10), latch_card.right - Scale(18), top + Scale(37)};
                FillRoundedRectangle(memory, count_badge, Scale(13), kSurfaceRaised, kBorder);
                DrawTextBlock(memory, L"×" + std::to_wstring(latch.instance_count), count_badge, small_font_, kMuted, DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
            }
        }
        if (state.latches.size() > 3) {
            RECT more{latch_card.left + Scale(20), latch_card.bottom - Scale(27), latch_card.right - Scale(20), latch_card.bottom - Scale(7)};
            DrawTextBlock(memory, L"+ " + std::to_wstring(state.latches.size() - 3) + L" more active", more, small_font_, kFaint, DT_RIGHT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
        }
    }

    RECT automation_title{margin, Scale(614), content_right, Scale(640)};
    DrawTextBlock(memory, L"Agent awareness", automation_title, body_semibold_font_, kText);
    RECT automation_hint{margin + Scale(116), Scale(614), content_right, Scale(640)};
    DrawTextBlock(memory, L"Click to enable or pause a provider", automation_hint, small_font_, kFaint);

    static constexpr std::array<Provider, 5> providers = {
        Provider::Codex,
        Provider::ClaudeCode,
        Provider::Cursor,
        Provider::OpenCode,
        Provider::GeminiCli,
    };
    for (std::size_t index = 0; index < providers.size(); ++index) {
        const Provider provider = providers[index];
        const bool enabled = state.settings.IsProviderEnabled(provider);
        const int row = index < 3 ? 0 : 1;
        const int column = index < 3 ? static_cast<int>(index) : static_cast<int>(index - 3);
        const int button_width_provider = row == 0 ? Scale(160) : Scale(244);
        const int left = margin + column * (button_width_provider + Scale(8));
        const int top = Scale(650) + row * Scale(48);
        RECT provider_button{left, top, left + button_width_provider, top + Scale(39)};
        FillRoundedRectangle(memory, provider_button, Scale(14), enabled ? kSurfaceRaised : kSurface, enabled ? ProviderColor(provider) : kBorder);
        DrawStatusDot(memory, provider_button.left + Scale(18), provider_button.top + Scale(19), Scale(4), enabled ? ProviderColor(provider) : kFaint);
        RECT provider_text{provider_button.left + Scale(31), provider_button.top, provider_button.right - Scale(12), provider_button.bottom};
        DrawTextBlock(memory, ProviderShortName(provider), provider_text, body_semibold_font_, enabled ? kText : kFaint);
        AddHitTarget(provider_button, ProviderAction(provider));
    }

    RECT footer{margin, Scale(758), content_right, Scale(812)};
    FillRoundedRectangle(memory, footer, Scale(14), kSurface, kBorder);
    const int half = (footer.right - footer.left) / 2;
    RECT display_area{footer.left, footer.top, footer.left + half, footer.bottom};
    RECT startup_area{footer.left + half, footer.top, footer.right, footer.bottom};
    RECT footer_divider{footer.left + half, footer.top + Scale(10), footer.left + half + 1, footer.bottom - Scale(10)};
    FillRectangle(memory, footer_divider, kBorder);
    DrawStatusDot(memory, display_area.left + Scale(20), display_area.top + Scale(27), Scale(5), state.keep_display_on ? kBlue : kFaint);
    RECT display_text{display_area.left + Scale(34), display_area.top + Scale(4), display_area.right - Scale(8), display_area.bottom};
    DrawTextBlock(memory, state.keep_display_on ? L"Display stays on" : L"Display may sleep", display_text, body_semibold_font_, state.keep_display_on ? kText : kMuted);
    DrawStatusDot(memory, startup_area.left + Scale(20), startup_area.top + Scale(27), Scale(5), state.start_with_windows ? kActive : kFaint);
    RECT startup_text{startup_area.left + Scale(34), startup_area.top + Scale(4), startup_area.right - Scale(8), startup_area.bottom};
    DrawTextBlock(memory, state.start_with_windows ? L"Starts with Windows" : L"Start manually", startup_text, body_semibold_font_, state.start_with_windows ? kText : kMuted);
    AddHitTarget(display_area, UiAction::ToggleDisplay);
    AddHitTarget(startup_area, UiAction::ToggleStartup);

    BitBlt(target, 0, 0, width, height, memory, 0, 0, SRCCOPY);
    SelectObject(memory, old_bitmap);
    DeleteObject(bitmap);
    DeleteDC(memory);
}

}  // namespace agent_latch
