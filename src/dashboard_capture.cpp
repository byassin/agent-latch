#include "dashboard_capture.h"
#include "win_util.h"

#include <wincodec.h>
#include <filesystem>

namespace agent_latch {
namespace {
struct CaptureContext {
    DashboardRenderer renderer;
    DashboardControls controls;
    DashboardState state;
};

LRESULT CALLBACK CaptureProcedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    auto* context = reinterpret_cast<CaptureContext*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        context = static_cast<CaptureContext*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(context));
    }
    if (context != nullptr) {
        if (message == WM_DRAWITEM && context->controls.DrawItem(reinterpret_cast<DRAWITEMSTRUCT*>(lparam))) { return TRUE; }
        if (message == WM_MEASUREITEM && context->controls.MeasureItem(reinterpret_cast<MEASUREITEMSTRUCT*>(lparam))) { return TRUE; }
        if (message == WM_CTLCOLORSTATIC || message == WM_CTLCOLORBTN || message == WM_CTLCOLORLISTBOX) {
            return reinterpret_cast<LRESULT>(context->controls.ControlColor(
                reinterpret_cast<HDC>(wparam), reinterpret_cast<HWND>(lparam)));
        }
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

bool SavePng(HBITMAP bitmap, const std::wstring& path) {
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    IWICImagingFactory* factory = nullptr;
    IWICStream* stream = nullptr;
    IWICBitmapEncoder* encoder = nullptr;
    IWICBitmapFrameEncode* frame = nullptr;
    IWICBitmap* source = nullptr;
    const bool success = [&]() {
        if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
            IID_IWICImagingFactory, reinterpret_cast<void**>(&factory))) ||
            FAILED(factory->CreateStream(&stream)) ||
            FAILED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE)) ||
            FAILED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)) ||
            FAILED(encoder->Initialize(stream, WICBitmapEncoderNoCache)) ||
            FAILED(encoder->CreateNewFrame(&frame, nullptr)) || FAILED(frame->Initialize(nullptr)) ||
            FAILED(factory->CreateBitmapFromHBITMAP(bitmap, nullptr, WICBitmapIgnoreAlpha, &source))) { return false; }
        UINT width = 0, height = 0;
        if (FAILED(source->GetSize(&width, &height)) || FAILED(frame->SetSize(width, height))) { return false; }
        WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGR;
        return SUCCEEDED(frame->SetPixelFormat(&format)) && SUCCEEDED(frame->WriteSource(source, nullptr)) &&
               SUCCEEDED(frame->Commit()) && SUCCEEDED(encoder->Commit());
    }();
    if (source != nullptr) { source->Release(); }
    if (frame != nullptr) { frame->Release(); }
    if (encoder != nullptr) { encoder->Release(); }
    if (stream != nullptr) { stream->Release(); }
    if (factory != nullptr) { factory->Release(); }
    if (SUCCEEDED(initialized)) { CoUninitialize(); }
    return success;
}
}  // namespace

bool ExportDashboardScreenshots(const DashboardState& state, const std::wstring& directory, UINT dpi) {
    if (directory.empty()) { return false; }
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    if (error) { return false; }
    CaptureContext context;
    context.state = state;
    context.renderer.Initialize(dpi);
    WNDCLASSW window_class{};
    window_class.lpfnWndProc = CaptureProcedure;
    window_class.hInstance = GetModuleHandleW(nullptr);
    window_class.lpszClassName = L"AgentLatch.OffscreenCapture.v1";
    if (RegisterClassW(&window_class) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) { return false; }
    const SIZE size = context.renderer.PreferredClientSize();
    HWND window = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, window_class.lpszClassName,
        L"AgentLatch off-screen capture", WS_POPUP, 0, 0, size.cx, size.cy,
        nullptr, nullptr, window_class.hInstance, &context);
    if (window == nullptr) { return false; }
    bool success = context.controls.Initialize(window, dpi);
    if (success) { context.controls.Update(state); }
    HDC dc = CreateCompatibleDC(nullptr);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = size.cx;
    info.bmiHeader.biHeight = -size.cy;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* pixels = nullptr;
    HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
    if (dc == nullptr || bitmap == nullptr) { success = false; }
    if (success) {
        HGDIOBJ previous = SelectObject(dc, bitmap);
        for (DashboardPage page : {DashboardPage::Overview, DashboardPage::Agents, DashboardPage::Settings}) {
            context.state.page = page;
            context.controls.SelectPage(page);
            context.renderer.Paint(dc, RECT{0, 0, size.cx, size.cy}, context.state);
            for (HWND child = GetWindow(window, GW_CHILD); child != nullptr; child = GetWindow(child, GW_HWNDNEXT)) {
                if ((GetWindowLongPtrW(child, GWL_STYLE) & WS_VISIBLE) == 0) { continue; }
                RECT bounds{};
                GetWindowRect(child, &bounds);
                MapWindowPoints(nullptr, window, reinterpret_cast<POINT*>(&bounds), 2);
                const int saved = SaveDC(dc);
                SetViewportOrgEx(dc, bounds.left, bounds.top, nullptr);
                IntersectClipRect(dc, 0, 0, bounds.right - bounds.left, bounds.bottom - bounds.top);
                SendMessageW(child, WM_PRINT, reinterpret_cast<WPARAM>(dc),
                             PRF_CLIENT | PRF_CHILDREN | PRF_ERASEBKGND);
                RestoreDC(dc, saved);
            }
            GdiFlush();
            const wchar_t* name = page == DashboardPage::Overview ? L"dashboard.png" :
                                 page == DashboardPage::Agents ? L"agents.png" : L"settings.png";
            success = SavePng(bitmap, JoinPath(directory, name)) && success;
        }
        SelectObject(dc, previous);
    }
    if (bitmap != nullptr) { DeleteObject(bitmap); }
    if (dc != nullptr) { DeleteDC(dc); }
    DestroyWindow(window);
    return success;
}

}  // namespace agent_latch
