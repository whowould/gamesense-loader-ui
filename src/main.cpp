#include <windows.h>
#include <dwmapi.h>
#include <d3d11.h>
#include <wrl/client.h>

#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>

#include "gui.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

using Microsoft::WRL::ComPtr;

namespace
{
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<IDXGISwapChain> swap_chain;
    ComPtr<ID3D11RenderTargetView> target;

    void create_target()
    {
        ComPtr<ID3D11Texture2D> back_buffer;
        swap_chain->GetBuffer(0, IID_PPV_ARGS(&back_buffer));
        device->CreateRenderTargetView(back_buffer.Get(), nullptr, &target);
    }

    bool create_device(HWND hwnd)
    {
        DXGI_SWAP_CHAIN_DESC desc{};
        desc.BufferCount = 2;
        desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.OutputWindow = hwnd;
        desc.SampleDesc.Count = 1;
        desc.Windowed = TRUE;
        desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

        constexpr D3D_DRIVER_TYPE drivers[] = { D3D_DRIVER_TYPE_HARDWARE, D3D_DRIVER_TYPE_WARP };
        for (auto driver : drivers)
        {
            if (SUCCEEDED(D3D11CreateDeviceAndSwapChain(nullptr, driver, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION,
                                                        &desc, &swap_chain, &device, nullptr, &context)))
            {
                create_target();
                return true;
            }
        }
        return false;
    }

    LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
    {
        if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wparam, lparam))
            return true;

        switch (msg)
        {
        case WM_SIZE:
            if (swap_chain && wparam != SIZE_MINIMIZED)
            {
                target.Reset();
                swap_chain->ResizeBuffers(0, LOWORD(lparam), HIWORD(lparam), DXGI_FORMAT_UNKNOWN, 0);
                create_target();
            }
            return 0;
        case WM_SYSCOMMAND:
            if ((wparam & 0xfff0) == SC_KEYMENU)
                return 0;
            break;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        }
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }

    void drag(HWND hwnd)
    {
        static bool dragging;
        static POINT grab;

        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsAnyItemHovered())
        {
            RECT rc;
            GetWindowRect(hwnd, &rc);
            GetCursorPos(&grab);
            grab.x -= rc.left;
            grab.y -= rc.top;
            dragging = true;
        }

        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
            dragging = false;

        if (dragging)
        {
            POINT cursor;
            GetCursorPos(&cursor);
            SetWindowPos(hwnd, nullptr, cursor.x - grab.x, cursor.y - grab.y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
        }
    }

    void fit(HWND hwnd, ImVec2 size)
    {
        RECT rc;
        GetWindowRect(hwnd, &rc);

        int w = static_cast<int>(size.x), h = static_cast<int>(size.y);
        if (rc.right - rc.left == w && rc.bottom - rc.top == h)
            return;

        SetWindowPos(hwnd, nullptr, (rc.left + rc.right - w) / 2, (rc.top + rc.bottom - h) / 2, w, h, SWP_NOZORDER);
    }
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int)
{
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    WNDCLASSEXW wc{ sizeof(wc) };
    wc.style = CS_CLASSDC;
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"loader";
    RegisterClassExW(&wc);

    ImVec2 size = gui::size();
    int w = static_cast<int>(size.x), h = static_cast<int>(size.y);
    int x = (GetSystemMetrics(SM_CXSCREEN) - w) / 2, y = (GetSystemMetrics(SM_CYSCREEN) - h) / 2;

    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"loader", WS_POPUP | WS_MINIMIZEBOX, x, y, w, h,
                                nullptr, nullptr, instance, nullptr);

    DWM_WINDOW_CORNER_PREFERENCE corners = DWMWCP_DONOTROUND;
    DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corners, sizeof(corners));

    if (!create_device(hwnd))
        return 1;

    ShowWindow(hwnd, SW_SHOWDEFAULT);
    UpdateWindow(hwnd);

    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(device.Get(), context.Get());
    gui::init(device.Get());

    for (bool running = true; running;)
    {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            if (msg.message == WM_QUIT)
                running = false;
        }

        if (!running)
            break;

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        running = gui::render();
        drag(hwnd);

        ImGui::Render();
        context->OMSetRenderTargets(1, target.GetAddressOf(), nullptr);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        swap_chain->Present(1, 0);

        fit(hwnd, gui::size());
    }

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, instance);
    CoUninitialize();
    return 0;
}
