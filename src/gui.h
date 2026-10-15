#pragma once

#include <d3d11.h>
#include <imgui.h>

namespace gui
{
    void init(ID3D11Device* device);
    bool render();
    ImVec2 size();
}
