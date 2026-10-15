#include "gui.h"

#include <imgui_internal.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace
{
    namespace col
    {
        constexpr ImU32 black   = IM_COL32(0, 0, 0, 255);
        constexpr ImU32 edge    = IM_COL32(60, 60, 60, 255);
        constexpr ImU32 border  = IM_COL32(40, 40, 40, 255);
        constexpr ImU32 outline = IM_COL32(48, 48, 48, 255);
        constexpr ImU32 bg      = IM_COL32(20, 20, 20, 255);
        constexpr ImU32 dots    = IM_COL32(15, 15, 15, 255);
        constexpr ImU32 group   = IM_COL32(23, 23, 23, 255);
        constexpr ImU32 header  = IM_COL32(26, 26, 26, 255);
        constexpr ImU32 panel   = IM_COL32(35, 35, 35, 255);
        constexpr ImU32 text    = IM_COL32(193, 193, 193, 255);
        constexpr ImU32 muted   = IM_COL32(173, 173, 173, 255);
        constexpr ImU32 accent  = IM_COL32(123, 194, 21, 255);
    }

    enum class page { login, main };

    struct entry
    {
        std::string text;
        bool highlight = false;
    };

    ID3D11Device* device;
    ComPtr<ID3D11ShaderResourceView> pattern, icon;
    ImFont* regular;
    ImFont* bold;

    page current = page::login;
    char username[32];
    char password[64];
    std::vector<entry> status;
    bool loading;

    ImTextureID id(const ComPtr<ID3D11ShaderResourceView>& srv)
    {
        return reinterpret_cast<ImTextureID>(srv.Get());
    }

    ComPtr<ID3D11ShaderResourceView> make_texture(const void* pixels, UINT w, UINT h)
    {
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = w;
        desc.Height = h;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_IMMUTABLE;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        D3D11_SUBRESOURCE_DATA data{ pixels, w * 4 };
        ComPtr<ID3D11Texture2D> texture;
        ComPtr<ID3D11ShaderResourceView> srv;
        if (SUCCEEDED(device->CreateTexture2D(&desc, &data, &texture)))
            device->CreateShaderResourceView(texture.Get(), nullptr, &srv);
        return srv;
    }

    ComPtr<ID3D11ShaderResourceView> load_texture(const std::filesystem::path& file)
    {
        ComPtr<IWICImagingFactory> wic;
        ComPtr<IWICBitmapDecoder> decoder;
        ComPtr<IWICBitmapFrameDecode> frame;
        ComPtr<IWICFormatConverter> converter;

        if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic)))
            || FAILED(wic->CreateDecoderFromFilename(file.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder))
            || FAILED(decoder->GetFrame(0, &frame))
            || FAILED(wic->CreateFormatConverter(&converter))
            || FAILED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom)))
            return nullptr;

        UINT w, h;
        converter->GetSize(&w, &h);

        std::vector<uint32_t> pixels(w * h);
        if (FAILED(converter->CopyPixels(nullptr, w * 4, w * h * 4, reinterpret_cast<BYTE*>(pixels.data()))))
            return nullptr;

        return make_texture(pixels.data(), w, h);
    }

    std::filesystem::path asset(const wchar_t* name)
    {
        wchar_t exe[MAX_PATH];
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        return std::filesystem::path(exe).parent_path() / L"assets" / name;
    }

    void text(ImDrawList* dl, ImVec2 pos, ImU32 color, const char* str, float size = 11.0f)
    {
        dl->AddText(bold, size, pos, color, str);
    }

    void window(ImDrawList* dl, ImVec2 size)
    {
        const struct { float inset; ImU32 color; } layers[] = {
            { 0, col::black }, { 1, col::edge }, { 2, col::border }, { 5, col::edge },
        };

        for (auto [inset, color] : layers)
            dl->AddRectFilled({ inset, inset }, { size.x - inset, size.y - inset }, color);

        ImVec2 min{ 6, 6 }, max = size - min;
        dl->AddImage(id(pattern), min, max, { 0, 0 }, (max - min) / 512.0f);

        constexpr ImU32 left = IM_COL32(59, 175, 222, 255);
        constexpr ImU32 mid = IM_COL32(202, 70, 205, 255);
        constexpr ImU32 right = IM_COL32(201, 227, 58, 255);

        float x = min.x + 1, y = min.y + 1, half = (max.x - min.x - 2) * 0.5f;
        dl->AddRectFilledMultiColor({ x, y }, { x + half, y + 2 }, left, mid, mid, left);
        dl->AddRectFilledMultiColor({ x + half, y }, { max.x - 1, y + 2 }, mid, right, right, mid);
        dl->AddRectFilled({ x, y + 1 }, { max.x - 1, y + 2 }, IM_COL32(0, 0, 0, 120));
        dl->AddRectFilled({ x, y + 2 }, { max.x - 1, y + 3 }, IM_COL32(8, 8, 8, 255));
    }

    void box(ImDrawList* dl, ImVec2 pos, ImVec2 size, ImU32 fill, const char* label = nullptr)
    {
        ImVec2 end = pos + size;
        dl->AddRectFilled(pos, end, fill);
        dl->AddRect(pos, end, col::black);
        dl->AddRect(pos + ImVec2(1, 1), end - ImVec2(1, 1), col::outline);

        if (!label)
            return;

        float w = bold->CalcTextSizeA(11.0f, FLT_MAX, 0.0f, label).x;
        dl->AddRectFilled({ pos.x + 10, pos.y }, { pos.x + 17 + w, pos.y + 2 }, col::dots);
        text(dl, { pos.x + 14, pos.y - 4 }, col::text, label);
    }

    bool button(const char* label, ImVec2 pos, ImVec2 size, float font_size = 11.0f)
    {
        ImGui::SetCursorPos(pos);
        bool pressed = ImGui::InvisibleButton(label, size);
        bool held = ImGui::IsItemActive();
        bool hovered = ImGui::IsItemHovered();

        ImU32 top = held ? IM_COL32(25, 25, 25, 255) : hovered ? IM_COL32(43, 43, 43, 255) : IM_COL32(35, 35, 35, 255);
        ImU32 bottom = held ? IM_COL32(35, 35, 35, 255) : hovered ? IM_COL32(31, 31, 31, 255) : IM_COL32(25, 25, 25, 255);

        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
        dl->AddRectFilledMultiColor(min, max, top, top, bottom, bottom);
        dl->AddRect(min, max, col::black);
        dl->AddRect(min + ImVec2(1, 1), max - ImVec2(1, 1), col::outline);

        ImVec2 ts = bold->CalcTextSizeA(font_size, FLT_MAX, 0.0f, label);
        text(dl, ImTrunc(min + (size - ts) * 0.5f + ImVec2(0.5f, 0.5f)), col::text, label, font_size);
        return pressed;
    }

    void caret(ImVec2 frame)
    {
        ImGuiInputTextState* state = ImGui::GetInputTextState(ImGui::GetItemID());
        if (!state || !ImGui::IsItemActive())
            return;

        if (state->CursorAnim > 0.0f && std::fmod(state->CursorAnim, 1.2f) > 0.8f)
            return;

        const char* str = state->TextA.Data;
        int cursor = state->GetCursorPos();
        float offset = state->Flags & ImGuiInputTextFlags_Password
            ? ImGui::CalcTextSize("*").x * ImTextCountCharsFromUtf8(str, str + cursor)
            : ImGui::CalcTextSize(str, str + cursor).x;

        float x = ImTrunc(frame.x + ImGui::GetStyle().FramePadding.x + offset - state->Scroll.x) + (offset > 0 ? 1 : -3);
        float y = frame.y + 4;

        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled({ x - 1, y + 1 }, { x, y + 11 }, IM_COL32(17, 17, 17, 255));
        dl->AddRectFilled({ x, y }, { x + 1, y + 12 }, IM_COL32(17, 17, 17, 255));
        dl->AddRectFilled({ x, y + 1 }, { x + 1, y + 11 }, IM_COL32(87, 137, 17, 255));
        dl->AddRectFilled({ x + 1, y + 1 }, { x + 2, y + 11 }, IM_COL32(61, 89, 22, 255));
    }

    bool field(const char* id, const char* hint, char* buf, size_t size, ImVec2 pos, ImGuiInputTextFlags flags = 0)
    {
        ImGui::PushFont(regular, 10.0f);
        ImGui::SetCursorPos(pos);
        ImGui::SetNextItemWidth(123);
        bool entered = ImGui::InputTextWithHint(id, hint, buf, size, flags | ImGuiInputTextFlags_EnterReturnsTrue);
        caret(ImGui::GetItemRectMin());
        ImGui::PopFont();
        return entered;
    }

    void login()
    {
        status = {
            { "Connected" },
            { "Welcome back, " + std::string(username) },
            { "Added Counter-Strike 2 (30 days remaining)" },
            { "Your session expires in 3 minutes" },
            { "Warning: Steam opened before client", true },
        };
        current = page::main;
    }

    bool login_page()
    {
        if (ImGui::IsWindowAppearing())
            ImGui::SetKeyboardFocusHere();

        bool submit = field("##username", "Username", username, sizeof(username), { 88, 53 });
        submit |= field("##password", "Password", password, sizeof(password), { 88, 82 }, ImGuiInputTextFlags_Password);

        if ((button("Login", { 88, 123 }, { 123, 23 }, 10.0f) || submit) && *username)
            login();

        return !button("Exit", { 88, 152 }, { 123, 23 }, 10.0f);
    }

    bool main_page(ImDrawList* dl)
    {
        box(dl, { 20, 21 }, { 273, 140 }, col::panel);
        dl->AddRectFilled({ 22, 23 }, { 291, 63 }, col::header);
        if (icon)
            dl->AddImage(id(icon), { 31, 28 }, { 59, 56 });
        text(dl, { 70, 28 }, col::accent, "Counter-Strike 2", 10.0f);
        text(dl, { 70, 43 }, col::text, "Updated 2023/01/10 1:43", 10.0f);

        box(dl, { 303, 21 }, { 192, 140 }, col::group, "Options");
        if (button("Load", { 318, 46 }, { 162, 40 }) && !loading)
        {
            status.push_back({ "Loading..." });
            loading = true;
        }
        if (button("Exit", { 318, 96 }, { 162, 40 }))
            return false;

        box(dl, { 20, 174 }, { 476, 201 }, col::group, "Status");
        box(dl, { 35, 188 }, { 446, 172 }, col::panel);

        float y = 192;
        for (const auto& [str, highlight] : status)
        {
            if (highlight)
                dl->AddRectFilled({ 38, y - 2 }, { 479, y + 13 }, col::header);
            text(dl, { 50, y }, highlight ? col::accent : col::muted, str.c_str());
            y += 15;
        }
        return true;
    }
}

void gui::init(ID3D11Device* dev)
{
    device = dev;

    std::vector<uint32_t> pixels(512 * 512);
    for (int y = 0; y < 512; ++y)
        for (int x = 0; x < 512; ++x)
            pixels[y * 512 + x] = x % 2 == 0 && y % 4 != x % 4 ? col::dots : col::bg;

    pattern = make_texture(pixels.data(), 512, 512);
    icon = load_texture(asset(L"icon.png"));

    ImFontConfig cfg;
    cfg.GlyphOffset.y = -1;

    ImGuiIO& io = ImGui::GetIO();
    regular = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\verdana.ttf", 10.0f, &cfg);
    bold = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\verdanab.ttf", 11.0f, &cfg);
    io.FontDefault = bold;

    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowPadding = { 0, 0 };
    style.WindowBorderSize = 0;
    style.FrameBorderSize = 1;
    style.FramePadding = { 6, 5 };
    style.AntiAliasedLines = false;
    style.AntiAliasedFill = false;

    ImVec4* c = style.Colors;
    c[ImGuiCol_Text] = ImColor(col::text);
    c[ImGuiCol_TextDisabled] = ImColor(85, 85, 85);
    c[ImGuiCol_FrameBg] = c[ImGuiCol_FrameBgHovered] = c[ImGuiCol_FrameBgActive] = ImColor(34, 34, 34);
    c[ImGuiCol_Border] = ImColor(col::black);
    c[ImGuiCol_TextSelectedBg] = ImColor(87, 137, 17, 110);
    c[ImGuiCol_InputTextCursor] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_NavCursor] = ImVec4(0, 0, 0, 0);
}

ImVec2 gui::size()
{
    return current == page::login ? ImVec2(300, 210) : ImVec2(520, 398);
}

bool gui::render()
{
    ImVec2 size = gui::size();
    ImGui::SetNextWindowPos({ 0, 0 });
    ImGui::SetNextWindowSize(size);
    ImGui::Begin("##loader", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBackground
                                     | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    window(dl, size);
    bool open = current == page::login ? login_page() : main_page(dl);

    ImGui::End();
    return open;
}
