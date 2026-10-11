// dear imgui: Renderer Backend for SDL_Renderer for SDL2
// (Requires: SDL 2.0.17+)

// Note that SDL_Renderer is an _optional_ component of SDL2, which IMHO is now largely obsolete.
// For a multi-platform app consider using other technologies:
// - SDL3+SDL_GPU: SDL_GPU is SDL3 new graphics abstraction API. You will need to update to SDL3.
// - SDL2+DirectX, SDL2+OpenGL, SDL2+Vulkan: combine SDL with dedicated renderers.
// If your application wants to render any non trivial amount of graphics other than UI,
// please be aware that SDL_Renderer currently offers a limited graphic API to the end-user
// and it might be difficult to step out of those boundaries.

// Implemented features:
//  [X] Renderer: User texture binding. Use 'SDL_Texture*' as ImTextureID. Read the FAQ about ImTextureID!
//  [X] Renderer: Large meshes support (64k+ vertices) even with 16-bit indices (ImGuiBackendFlags_RendererHasVtxOffset).
//  [X] Renderer: Expose selected render state for draw callbacks to use. Access in '(ImGui_ImplXXXX_RenderState*)GetPlatformIO().Renderer_RenderState'.

// You can copy and use unmodified imgui_impl_* files in your project. See examples/ folder for examples of using this.
// Prefer including the entire imgui/ repository into your project (either as a copy or as a submodule), and only build the backends you need.
// Learn about Dear ImGui:
// - FAQ                  https://dearimgui.com/faq
// - Getting Started      https://dearimgui.com/getting-started
// - Documentation        https://dearimgui.com/docs (same as your local docs/ folder).
// - Introduction, links and more at the top of imgui.cpp

// CHANGELOG
//  2025-01-18: Use endian-dependent RGBA32 texture format, to match SDL_Color.
//  2024-10-09: Expose selected render state in ImGui_ImplSDLRenderer2_RenderState, which you can access in 'void* platform_io.Renderer_RenderState' during draw callbacks.
//  2024-05-14: *BREAKING CHANGE* ImGui_ImplSDLRenderer3_RenderDrawData() requires SDL_Renderer* passed as parameter.
//  2023-05-30: Renamed imgui_impl_sdlrenderer.h/.cpp to imgui_impl_sdlrenderer2.h/.cpp to accommodate for upcoming SDL3.
//  2022-10-11: Using 'nullptr' instead of 'NULL' as per our switch to C++11.
//  2021-12-21: Update SDL_RenderGeometryRaw() format to work with SDL 2.0.19.
//  2021-12-03: Added support for large mesh (64K+ vertices), enable ImGuiBackendFlags_RendererHasVtxOffset flag.
//  2021-10-06: Backup and restore modified ClipRect/Viewport.
//  2021-09-21: Initial version.

#include "imgui.h"
#ifndef IMGUI_DISABLE
#include "imgui_impl_sdlrenderer2.h"
#include <stdint.h>     // intptr_t

// Clang warnings with -Weverything
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wsign-conversion"    // warning: implicit conversion changes signedness
#endif

// SDL
#include <SDL.h>
// xanatoo R36S / ArkOS: eoan ships SDL 2.0.10. Upstream ImGui requires
// 2.0.17+ for SDL_RenderGeometry; use a software triangle path instead.
#if !SDL_VERSION_ATLEAST(2,0,17)
#define IMGUI_SDL_RENDERER_LEGACY 1
#endif
#ifndef IMGUI_SDL_RENDERER_LEGACY
#define IMGUI_SDL_RENDERER_LEGACY 0
#endif

// SDL_Renderer data
struct ImGui_ImplSDLRenderer2_Data
{
    SDL_Renderer*   Renderer;       // Main viewport's renderer
    SDL_Texture*    FontTexture;
    ImGui_ImplSDLRenderer2_Data()   { memset((void*)this, 0, sizeof(*this)); }
};

// Backend data stored in io.BackendRendererUserData to allow support for multiple Dear ImGui contexts
// It is STRONGLY preferred that you use docking branch with multi-viewports (== single Dear ImGui context + multiple windows) instead of multiple Dear ImGui contexts.
static ImGui_ImplSDLRenderer2_Data* ImGui_ImplSDLRenderer2_GetBackendData()
{
    return ImGui::GetCurrentContext() ? (ImGui_ImplSDLRenderer2_Data*)ImGui::GetIO().BackendRendererUserData : nullptr;
}

// Functions
bool ImGui_ImplSDLRenderer2_Init(SDL_Renderer* renderer)
{
    ImGuiIO& io = ImGui::GetIO();
    IMGUI_CHECKVERSION();
    IM_ASSERT(io.BackendRendererUserData == nullptr && "Already initialized a renderer backend!");
    IM_ASSERT(renderer != nullptr && "SDL_Renderer not initialized!");

    // Setup backend capabilities flags
    ImGui_ImplSDLRenderer2_Data* bd = IM_NEW(ImGui_ImplSDLRenderer2_Data)();
    io.BackendRendererUserData = (void*)bd;
    io.BackendRendererName = "imgui_impl_sdlrenderer2";
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;  // We can honor the ImDrawCmd::VtxOffset field, allowing for large meshes.

    bd->Renderer = renderer;

    return true;
}

void ImGui_ImplSDLRenderer2_Shutdown()
{
    ImGui_ImplSDLRenderer2_Data* bd = ImGui_ImplSDLRenderer2_GetBackendData();
    IM_ASSERT(bd != nullptr && "No renderer backend to shutdown, or already shutdown?");
    ImGuiIO& io = ImGui::GetIO();

    ImGui_ImplSDLRenderer2_DestroyDeviceObjects();

    io.BackendRendererName = nullptr;
    io.BackendRendererUserData = nullptr;
    io.BackendFlags &= ~ImGuiBackendFlags_RendererHasVtxOffset;
    IM_DELETE(bd);
}

static void ImGui_ImplSDLRenderer2_SetupRenderState(SDL_Renderer* renderer)
{
	// Clear out any viewports and cliprect set by the user
    // FIXME: Technically speaking there are lots of other things we could backup/setup/restore during our render process.
	SDL_RenderSetViewport(renderer, nullptr);
	SDL_RenderSetClipRect(renderer, nullptr);
}

void ImGui_ImplSDLRenderer2_NewFrame()
{
    ImGui_ImplSDLRenderer2_Data* bd = ImGui_ImplSDLRenderer2_GetBackendData();
    IM_ASSERT(bd != nullptr && "Context or backend not initialized! Did you call ImGui_ImplSDLRenderer2_Init()?");

    if (!bd->FontTexture)
        ImGui_ImplSDLRenderer2_CreateDeviceObjects();
}


#if IMGUI_SDL_RENDERER_LEGACY
// Minimal textured-triangle path for SDL < 2.0.17 (no RenderGeometry).
// Adequate for ImGui menus on low-res handhelds (R36S 640x480).
static void ImGui_ImplSDLRenderer2_RenderGeometryLegacy(
    SDL_Renderer* renderer, SDL_Texture* texture,
    const float* xy, int xy_stride,
    const int* color, int color_stride,
    const float* uv, int uv_stride,
    int /*num_vertices*/,
    const void* indices, int num_indices, int size_indices)
{
    auto idx_at = [&](int i) -> unsigned int {
        const char* base = (const char*)indices;
        if (size_indices == 2)
            return (unsigned int)(*(const ImU16*)(base + i * 2));
        return *(const unsigned int*)(base + i * 4);
    };
    auto vert = [&](unsigned int i, float& x, float& y, float& u, float& v, int& col) {
        const char* pxy = (const char*)xy + i * xy_stride;
        const char* puv = (const char*)uv + i * uv_stride;
        const char* pcol = (const char*)color + i * color_stride;
        x = ((const float*)pxy)[0];
        y = ((const float*)pxy)[1];
        u = ((const float*)puv)[0];
        v = ((const float*)puv)[1];
        col = *(const int*)pcol;
    };

    int tw = 1, th = 1;
    if (texture)
        SDL_QueryTexture(texture, nullptr, nullptr, &tw, &th);

    for (int i = 0; i + 2 < num_indices; i += 3)
    {
        float x0, y0, u0, v0, x1, y1, u1, v1, x2, y2, u2, v2;
        int c0, c1, c2;
        vert(idx_at(i + 0), x0, y0, u0, v0, c0);
        vert(idx_at(i + 1), x1, y1, u1, v1, c1);
        vert(idx_at(i + 2), x2, y2, u2, v2, c2);

        // Axis-aligned bounds → single textured rect (ImGui UI is mostly quads).
        float minx = x0 < x1 ? (x0 < x2 ? x0 : x2) : (x1 < x2 ? x1 : x2);
        float maxx = x0 > x1 ? (x0 > x2 ? x0 : x2) : (x1 > x2 ? x1 : x2);
        float miny = y0 < y1 ? (y0 < y2 ? y0 : y2) : (y1 < y2 ? y1 : y2);
        float maxy = y0 > y1 ? (y0 > y2 ? y0 : y2) : (y1 > y2 ? y1 : y2);
        float minu = u0 < u1 ? (u0 < u2 ? u0 : u2) : (u1 < u2 ? u1 : u2);
        float maxu = u0 > u1 ? (u0 > u2 ? u0 : u2) : (u1 > u2 ? u1 : u2);
        float minv = v0 < v1 ? (v0 < v2 ? v0 : v2) : (v1 < v2 ? v1 : v2);
        float maxv = v0 > v1 ? (v0 > v2 ? v0 : v2) : (v1 > v2 ? v1 : v2);

        const int a = (c0 >> 24) & 0xff;
        const int r = (c0 >> 0) & 0xff;
        const int g = (c0 >> 8) & 0xff;
        const int b = (c0 >> 16) & 0xff;
        SDL_SetRenderDrawColor(renderer, r, g, b, a);

        if (texture && (maxu - minu) > 1e-6f && (maxv - minv) > 1e-6f)
        {
            SDL_Rect src;
            src.x = (int)(minu * tw);
            src.y = (int)(minv * th);
            src.w = (int)((maxu - minu) * tw);
            src.h = (int)((maxv - minv) * th);
            if (src.w < 1) src.w = 1;
            if (src.h < 1) src.h = 1;
            SDL_Rect dst;
            dst.x = (int)minx;
            dst.y = (int)miny;
            dst.w = (int)(maxx - minx);
            dst.h = (int)(maxy - miny);
            if (dst.w < 1) dst.w = 1;
            if (dst.h < 1) dst.h = 1;
            SDL_SetTextureColorMod(texture, r, g, b);
            SDL_SetTextureAlphaMod(texture, a);
            SDL_RenderCopy(renderer, texture, &src, &dst);
        }
        else
        {
            SDL_Rect dst;
            dst.x = (int)minx;
            dst.y = (int)miny;
            dst.w = (int)(maxx - minx);
            dst.h = (int)(maxy - miny);
            if (dst.w < 1) dst.w = 1;
            if (dst.h < 1) dst.h = 1;
            SDL_RenderFillRect(renderer, &dst);
        }
    }
    if (texture)
    {
        SDL_SetTextureColorMod(texture, 255, 255, 255);
        SDL_SetTextureAlphaMod(texture, 255);
    }
}
#endif

void ImGui_ImplSDLRenderer2_RenderDrawData(ImDrawData* draw_data, SDL_Renderer* renderer)
{
	// If there's a scale factor set by the user, use that instead
    // If the user has specified a scale factor to SDL_Renderer already via SDL_RenderSetScale(), SDL will scale whatever we pass
    // to SDL_RenderGeometryRaw() by that scale factor. In that case we don't want to be also scaling it ourselves here.
    float rsx = 1.0f;
	float rsy = 1.0f;
	SDL_RenderGetScale(renderer, &rsx, &rsy);
    ImVec2 render_scale;
	render_scale.x = (rsx == 1.0f) ? draw_data->FramebufferScale.x : 1.0f;
	render_scale.y = (rsy == 1.0f) ? draw_data->FramebufferScale.y : 1.0f;

	// Avoid rendering when minimized, scale coordinates for retina displays (screen coordinates != framebuffer coordinates)
	int fb_width = (int)(draw_data->DisplaySize.x * render_scale.x);
	int fb_height = (int)(draw_data->DisplaySize.y * render_scale.y);
	if (fb_width == 0 || fb_height == 0)
		return;

    // Backup SDL_Renderer state that will be modified to restore it afterwards
    struct BackupSDLRendererState
    {
        SDL_Rect    Viewport;
        bool        ClipEnabled;
        SDL_Rect    ClipRect;
    };
    BackupSDLRendererState old = {};
    old.ClipEnabled = SDL_RenderIsClipEnabled(renderer) == SDL_TRUE;
    SDL_RenderGetViewport(renderer, &old.Viewport);
    SDL_RenderGetClipRect(renderer, &old.ClipRect);

    // Setup desired state
    ImGui_ImplSDLRenderer2_SetupRenderState(renderer);

    // Setup render state structure (for callbacks and custom texture bindings)
    ImGuiPlatformIO& platform_io = ImGui::GetPlatformIO();
    ImGui_ImplSDLRenderer2_RenderState render_state;
    render_state.Renderer = renderer;
    platform_io.Renderer_RenderState = &render_state;

	// Will project scissor/clipping rectangles into framebuffer space
	ImVec2 clip_off = draw_data->DisplayPos;         // (0,0) unless using multi-viewports
	ImVec2 clip_scale = render_scale;

    // Render command lists
    for (int n = 0; n < draw_data->CmdListsCount; n++)
    {
        const ImDrawList* draw_list = draw_data->CmdLists[n];
        const ImDrawVert* vtx_buffer = draw_list->VtxBuffer.Data;
        const ImDrawIdx* idx_buffer = draw_list->IdxBuffer.Data;

        for (int cmd_i = 0; cmd_i < draw_list->CmdBuffer.Size; cmd_i++)
        {
            const ImDrawCmd* pcmd = &draw_list->CmdBuffer[cmd_i];
            if (pcmd->UserCallback)
            {
                // User callback, registered via ImDrawList::AddCallback()
                // (ImDrawCallback_ResetRenderState is a special callback value used by the user to request the renderer to reset render state.)
                if (pcmd->UserCallback == ImDrawCallback_ResetRenderState)
                    ImGui_ImplSDLRenderer2_SetupRenderState(renderer);
                else
                    pcmd->UserCallback(draw_list, pcmd);
            }
            else
            {
                // Project scissor/clipping rectangles into framebuffer space
                ImVec2 clip_min((pcmd->ClipRect.x - clip_off.x) * clip_scale.x, (pcmd->ClipRect.y - clip_off.y) * clip_scale.y);
                ImVec2 clip_max((pcmd->ClipRect.z - clip_off.x) * clip_scale.x, (pcmd->ClipRect.w - clip_off.y) * clip_scale.y);
                if (clip_min.x < 0.0f) { clip_min.x = 0.0f; }
                if (clip_min.y < 0.0f) { clip_min.y = 0.0f; }
                if (clip_max.x > (float)fb_width) { clip_max.x = (float)fb_width; }
                if (clip_max.y > (float)fb_height) { clip_max.y = (float)fb_height; }
                if (clip_max.x <= clip_min.x || clip_max.y <= clip_min.y)
                    continue;

                SDL_Rect r = { (int)(clip_min.x), (int)(clip_min.y), (int)(clip_max.x - clip_min.x), (int)(clip_max.y - clip_min.y) };
                SDL_RenderSetClipRect(renderer, &r);

                const float* xy = (const float*)(const void*)((const char*)(vtx_buffer + pcmd->VtxOffset) + offsetof(ImDrawVert, pos));
                const float* uv = (const float*)(const void*)((const char*)(vtx_buffer + pcmd->VtxOffset) + offsetof(ImDrawVert, uv));
#if SDL_VERSION_ATLEAST(2,0,19)
                const SDL_Color* color = (const SDL_Color*)(const void*)((const char*)(vtx_buffer + pcmd->VtxOffset) + offsetof(ImDrawVert, col)); // SDL 2.0.19+
#else
                const int* color = (const int*)(const void*)((const char*)(vtx_buffer + pcmd->VtxOffset) + offsetof(ImDrawVert, col)); // SDL 2.0.17 and 2.0.18
#endif

                                // Bind texture, Draw
				SDL_Texture* tex = (SDL_Texture*)pcmd->GetTexID();
#if IMGUI_SDL_RENDERER_LEGACY
                ImGui_ImplSDLRenderer2_RenderGeometryLegacy(renderer, tex,
                    xy, (int)sizeof(ImDrawVert),
                    color, (int)sizeof(ImDrawVert),
                    uv, (int)sizeof(ImDrawVert),
                    draw_list->VtxBuffer.Size - pcmd->VtxOffset,
                    idx_buffer + pcmd->IdxOffset, pcmd->ElemCount, sizeof(ImDrawIdx));
#else
                SDL_RenderGeometryRaw(renderer, tex,
                    xy, (int)sizeof(ImDrawVert),
                    color, (int)sizeof(ImDrawVert),
                    uv, (int)sizeof(ImDrawVert),
                    draw_list->VtxBuffer.Size - pcmd->VtxOffset,
                    idx_buffer + pcmd->IdxOffset, pcmd->ElemCount, sizeof(ImDrawIdx));
#endif

            }
        }
    }
    platform_io.Renderer_RenderState = nullptr;

    // Restore modified SDL_Renderer state
    SDL_RenderSetViewport(renderer, &old.Viewport);
    SDL_RenderSetClipRect(renderer, old.ClipEnabled ? &old.ClipRect : nullptr);
}

// Called by Init/NewFrame/Shutdown
bool ImGui_ImplSDLRenderer2_CreateFontsTexture()
{
    ImGuiIO& io = ImGui::GetIO();
    ImGui_ImplSDLRenderer2_Data* bd = ImGui_ImplSDLRenderer2_GetBackendData();

    // Build texture atlas
    unsigned char* pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);   // Load as RGBA 32-bit (75% of the memory is wasted, but default font is so small) because it is more likely to be compatible with user's existing shaders. If your ImTextureId represent a higher-level concept than just a GL texture id, consider calling GetTexDataAsAlpha8() instead to save on GPU memory.

    // Upload texture to graphics system
    // Nearest sampling so ProggyClean / UI glyphs stay sharp on low-res LCDs
    // (bilinear turns bitmap fonts to mush). Pair with style.AntiAliasedLinesUseTex=false.
    bd->FontTexture = SDL_CreateTexture(bd->Renderer,
#if defined(SDL_PIXELFORMAT_RGBA32)
        SDL_PIXELFORMAT_RGBA32,
#else
        SDL_PIXELFORMAT_RGBA8888,
#endif
        SDL_TEXTUREACCESS_STATIC, width, height);
    if (bd->FontTexture == nullptr)
    {
        SDL_Log("error creating texture");
        return false;
    }
    SDL_UpdateTexture(bd->FontTexture, nullptr, pixels, 4 * width);
    SDL_SetTextureBlendMode(bd->FontTexture, SDL_BLENDMODE_BLEND);
#if SDL_VERSION_ATLEAST(2,0,12)
    SDL_SetTextureScaleMode(bd->FontTexture, SDL_ScaleModeNearest);
#endif

    // Store our identifier
    io.Fonts->SetTexID((ImTextureID)(intptr_t)bd->FontTexture);

    return true;
}

void ImGui_ImplSDLRenderer2_DestroyFontsTexture()
{
    ImGuiIO& io = ImGui::GetIO();
    ImGui_ImplSDLRenderer2_Data* bd = ImGui_ImplSDLRenderer2_GetBackendData();
    if (bd->FontTexture)
    {
        io.Fonts->SetTexID(0);
        SDL_DestroyTexture(bd->FontTexture);
        bd->FontTexture = nullptr;
    }
}

bool ImGui_ImplSDLRenderer2_CreateDeviceObjects()
{
    return ImGui_ImplSDLRenderer2_CreateFontsTexture();
}

void ImGui_ImplSDLRenderer2_DestroyDeviceObjects()
{
    ImGui_ImplSDLRenderer2_DestroyFontsTexture();
}

//-----------------------------------------------------------------------------

#if defined(__clang__)
#pragma clang diagnostic pop
#endif

#endif // #ifndef IMGUI_DISABLE
