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
#include "imgui_internal.h" // ImHashData
#include <stdint.h>     // intptr_t
#include <stdio.h>      // fprintf (xanatoo: fallback log lines)

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
#if IMGUI_SDL_RENDERER_LEGACY
// Built against SDL < 2.0.17 headers, but the SDL loaded at runtime may be
// newer (ArkOS ships its own build): use its SDL_RenderGeometryRaw when it
// has one (2.0.18+) and fall back to the per-triangle path below otherwise.
typedef int (SDLCALL *ImGui_ImplSDLRenderer2_GeometryRawFn)(
    SDL_Renderer*, SDL_Texture*, const float*, int, const void*, int,
    const float*, int, int, const void*, int, int);
static ImGui_ImplSDLRenderer2_GeometryRawFn g_RenderGeometryRaw = nullptr;

static ImGui_ImplSDLRenderer2_GeometryRawFn ImGui_ImplSDLRenderer2_LookupGeometryRaw()
{
    SDL_version v;
    SDL_GetVersion(&v);
    ImGui_ImplSDLRenderer2_GeometryRawFn fn = nullptr;
#if defined(__linux__) && !defined(IMGUI_SDL_RENDERER_FORCE_FALLBACK)
    // Opening the loaded library by its soname returns the same handle.
    if (SDL_VERSIONNUM(v.major, v.minor, v.patch) >= SDL_VERSIONNUM(2, 0, 18))
        if (void* lib = SDL_LoadObject("libSDL2-2.0.so.0"))
            fn = (ImGui_ImplSDLRenderer2_GeometryRawFn)SDL_LoadFunction(lib, "SDL_RenderGeometryRaw");
#endif
    fprintf(stderr, "imgui: SDL %d.%d.%d at runtime, %s\n", v.major, v.minor, v.patch,
            fn ? "using SDL_RenderGeometryRaw" : "per-triangle fallback renderer");
    return fn;
}
#endif

// SDL_Renderer data
struct ImGui_ImplSDLRenderer2_Data
{
    SDL_Renderer*   Renderer;       // Main viewport's renderer
    SDL_Texture*    FontTexture;
#if IMGUI_SDL_RENDERER_LEGACY
    // Fallback renderer: UI drawn into Cache, redrawn when CacheHash changes.
    SDL_Texture*    Cache;
    int             CacheW, CacheH;
    bool            CacheValid, CacheFailed;
    ImGuiID         CacheHash;
    SDL_Rect        CacheRect;
#endif
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
#if IMGUI_SDL_RENDERER_LEGACY
    g_RenderGeometryRaw = ImGui_ImplSDLRenderer2_LookupGeometryRaw();
#endif

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
// Minimal textured-triangle path for SDL < 2.0.17 (no RenderGeometry): each
// triangle becomes the rectangle of its bounding box, one SDL call. Right for
// the axis-aligned quads ImGui emits without anti-aliasing and rounding
// (ImGui_ImplSDLRenderer2_UsesTriangleFallback); a quad's two triangles cover
// the same rectangle and are drawn once.
struct ImGui_ImplSDLRenderer2_TriRect
{
    float x0, y0, x1, y1, u0, v0, u1, v1;
    int col;
};

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
    auto tri_rect = [&](int i, ImGui_ImplSDLRenderer2_TriRect& t) {
        for (int k = 0; k < 3; k++)
        {
            const unsigned int vi = idx_at(i + k);
            const float* pxy = (const float*)((const char*)xy + vi * xy_stride);
            const float* puv = (const float*)((const char*)uv + vi * uv_stride);
            if (k == 0)
            {
                t.x0 = t.x1 = pxy[0]; t.y0 = t.y1 = pxy[1];
                t.u0 = t.u1 = puv[0]; t.v0 = t.v1 = puv[1];
                t.col = *(const int*)((const char*)color + vi * color_stride);
                continue;
            }
            t.x0 = ImMin(t.x0, pxy[0]); t.x1 = ImMax(t.x1, pxy[0]);
            t.y0 = ImMin(t.y0, pxy[1]); t.y1 = ImMax(t.y1, pxy[1]);
            t.u0 = ImMin(t.u0, puv[0]); t.u1 = ImMax(t.u1, puv[0]);
            t.v0 = ImMin(t.v0, puv[1]); t.v1 = ImMax(t.v1, puv[1]);
        }
    };

    int tw = 1, th = 1;
    if (texture)
        SDL_QueryTexture(texture, nullptr, nullptr, &tw, &th);

    SDL_BlendMode old_blend;
    SDL_GetRenderDrawBlendMode(renderer, &old_blend);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    bool mod_set = false;
    int mod_col = 0;

    for (int i = 0; i + 2 < num_indices; )
    {
        ImGui_ImplSDLRenderer2_TriRect t;
        tri_rect(i, t);
        i += 3;
        if (i + 2 < num_indices)
        {
            ImGui_ImplSDLRenderer2_TriRect next;
            tri_rect(i, next);
            if (memcmp(&t, &next, sizeof(t)) == 0)
                i += 3;
        }

        const Uint8 a = (Uint8)((t.col >> 24) & 0xff);
        const Uint8 r = (Uint8)((t.col >> 0) & 0xff);
        const Uint8 g = (Uint8)((t.col >> 8) & 0xff);
        const Uint8 b = (Uint8)((t.col >> 16) & 0xff);

        SDL_Rect dst;
        dst.x = (int)t.x0;
        dst.y = (int)t.y0;
        dst.w = (int)(t.x1 - t.x0);
        dst.h = (int)(t.y1 - t.y0);
        if (dst.w < 1) dst.w = 1;
        if (dst.h < 1) dst.h = 1;

        if (texture && (t.u1 - t.u0) > 1e-6f && (t.v1 - t.v0) > 1e-6f)
        {
            SDL_Rect src;
            src.x = (int)(t.u0 * tw);
            src.y = (int)(t.v0 * th);
            src.w = (int)((t.u1 - t.u0) * tw);
            src.h = (int)((t.v1 - t.v0) * th);
            if (src.w < 1) src.w = 1;
            if (src.h < 1) src.h = 1;
            if (!mod_set || t.col != mod_col)
            {
                SDL_SetTextureColorMod(texture, r, g, b);
                SDL_SetTextureAlphaMod(texture, a);
                mod_set = true;
                mod_col = t.col;
            }
            SDL_RenderCopy(renderer, texture, &src, &dst);
        }
        else
        {
            SDL_SetRenderDrawColor(renderer, r, g, b, a);
            SDL_RenderFillRect(renderer, &dst);
        }
    }
    // Restore so other code does not inherit the tint.
    if (mod_set)
    {
        SDL_SetTextureColorMod(texture, 255, 255, 255);
        SDL_SetTextureAlphaMod(texture, 255);
    }
    SDL_SetRenderDrawBlendMode(renderer, old_blend);
}

// Hash of everything that decides what the draw data puts on screen; also
// returns the bounding box of the clipped commands in framebuffer pixels.
static ImGuiID ImGui_ImplSDLRenderer2_HashDrawData(ImDrawData* draw_data, ImVec2 clip_off, ImVec2 clip_scale,
                                                   int fb_width, int fb_height, SDL_Rect* bounds)
{
    ImGuiID h = ImHashData(&draw_data->DisplaySize, sizeof(draw_data->DisplaySize));
    ImVec2 bmin((float)fb_width, (float)fb_height), bmax(0.0f, 0.0f);
    for (int n = 0; n < draw_data->CmdListsCount; n++)
    {
        const ImDrawList* draw_list = draw_data->CmdLists[n];
        h = ImHashData(draw_list->VtxBuffer.Data, (size_t)draw_list->VtxBuffer.Size * sizeof(ImDrawVert), h);
        h = ImHashData(draw_list->IdxBuffer.Data, (size_t)draw_list->IdxBuffer.Size * sizeof(ImDrawIdx), h);
        for (const ImDrawCmd& cmd : draw_list->CmdBuffer)
        {
            const ImTextureID tex = cmd.GetTexID();
            h = ImHashData(&cmd.ClipRect, sizeof(cmd.ClipRect), h);
            h = ImHashData(&tex, sizeof(tex), h);
            h = ImHashData(&cmd.VtxOffset, sizeof(cmd.VtxOffset), h);
            h = ImHashData(&cmd.IdxOffset, sizeof(cmd.IdxOffset), h);
            h = ImHashData(&cmd.ElemCount, sizeof(cmd.ElemCount), h);
            h = ImHashData(&cmd.UserCallback, sizeof(cmd.UserCallback), h);
            if (cmd.ElemCount == 0)
                continue;
            bmin.x = ImMin(bmin.x, (cmd.ClipRect.x - clip_off.x) * clip_scale.x);
            bmin.y = ImMin(bmin.y, (cmd.ClipRect.y - clip_off.y) * clip_scale.y);
            bmax.x = ImMax(bmax.x, (cmd.ClipRect.z - clip_off.x) * clip_scale.x);
            bmax.y = ImMax(bmax.y, (cmd.ClipRect.w - clip_off.y) * clip_scale.y);
        }
    }
    bmin = ImMax(bmin, ImVec2(0.0f, 0.0f));
    bmax = ImMin(bmax, ImVec2((float)fb_width, (float)fb_height));
    bounds->x = (int)bmin.x;
    bounds->y = (int)bmin.y;
    bounds->w = ImMax(0, (int)ImCeil(bmax.x) - bounds->x);
    bounds->h = ImMax(0, (int)ImCeil(bmax.y) - bounds->y);
    return h;
}

// Target texture the fallback draws the UI into (premultiplied alpha), or
// nullptr when SDL cannot render to textures or applies its own scale.
static SDL_Texture* ImGui_ImplSDLRenderer2_UiCache(ImGui_ImplSDLRenderer2_Data* bd, SDL_Renderer* renderer,
                                                   int fb_width, int fb_height, float rsx, float rsy)
{
    if (bd->CacheFailed || rsx != 1.0f || rsy != 1.0f || SDL_GetRenderTarget(renderer) != nullptr)
        return nullptr;
    if (bd->Cache && bd->CacheW == fb_width && bd->CacheH == fb_height)
        return bd->Cache;
    if (bd->Cache)
        SDL_DestroyTexture(bd->Cache);
    bd->Cache = nullptr;
    bd->CacheValid = false;
    if (SDL_RenderTargetSupported(renderer))
        bd->Cache = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, fb_width, fb_height);
    if (!bd->Cache)
    {
        fprintf(stderr, "imgui: no render target for the UI cache, drawing every frame\n");
        bd->CacheFailed = true;
        return nullptr;
    }
    const SDL_BlendMode premultiplied = SDL_ComposeCustomBlendMode(
        SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA, SDL_BLENDOPERATION_ADD,
        SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA, SDL_BLENDOPERATION_ADD);
    if (SDL_SetTextureBlendMode(bd->Cache, premultiplied) != 0)
        SDL_SetTextureBlendMode(bd->Cache, SDL_BLENDMODE_BLEND);
    bd->CacheW = fb_width;
    bd->CacheH = fb_height;
    return bd->Cache;
}
#endif

bool ImGui_ImplSDLRenderer2_UsesTriangleFallback()
{
#if IMGUI_SDL_RENDERER_LEGACY
    return g_RenderGeometryRaw == nullptr;
#else
    return false;
#endif
}

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

    int list_count = draw_data->CmdListsCount;
#if IMGUI_SDL_RENDERER_LEGACY
    // One SDL call per triangle is slow (a draw call each on GLES) and the UI
    // rarely changes while the page scrolls: draw it into a texture only when
    // the draw data changed and blend that texture in on every frame.
    ImGui_ImplSDLRenderer2_Data* bd = ImGui_ImplSDLRenderer2_GetBackendData();
    SDL_Texture* cache = g_RenderGeometryRaw ? nullptr
        : ImGui_ImplSDLRenderer2_UiCache(bd, renderer, fb_width, fb_height, rsx, rsy);
    bool to_cache = false;
    if (cache)
    {
        SDL_Rect bounds;
        const ImGuiID hash = ImGui_ImplSDLRenderer2_HashDrawData(draw_data, clip_off, clip_scale, fb_width, fb_height, &bounds);
        if (bd->CacheValid && hash == bd->CacheHash)
        {
            list_count = 0;
        }
        else
        {
            bd->CacheHash = hash;
            bd->CacheValid = true;
            bd->CacheRect = bounds;
            to_cache = true;
            SDL_SetRenderTarget(renderer, cache);
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
            SDL_RenderClear(renderer);
        }
    }
#endif

    // Render command lists
    for (int n = 0; n < list_count; n++)
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
                if (g_RenderGeometryRaw)
                    g_RenderGeometryRaw(renderer, tex,
                        xy, (int)sizeof(ImDrawVert),
                        color, (int)sizeof(ImDrawVert),
                        uv, (int)sizeof(ImDrawVert),
                        draw_list->VtxBuffer.Size - pcmd->VtxOffset,
                        idx_buffer + pcmd->IdxOffset, pcmd->ElemCount, sizeof(ImDrawIdx));
                else
                    ImGui_ImplSDLRenderer2_RenderGeometryLegacy(renderer, tex,
                        xy, (int)sizeof(ImDrawVert),
                        (const int*)(const void*)color, (int)sizeof(ImDrawVert),
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

#if IMGUI_SDL_RENDERER_LEGACY
    if (cache)
    {
        if (to_cache)
            SDL_SetRenderTarget(renderer, nullptr);
        ImGui_ImplSDLRenderer2_SetupRenderState(renderer);
        if (bd->CacheRect.w > 0 && bd->CacheRect.h > 0)
            SDL_RenderCopy(renderer, cache, &bd->CacheRect, &bd->CacheRect);
    }
#endif

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

    // Upload via CreateRGBSurfaceFrom + CreateTextureFromSurface — same path
    // xanatoo FreeType glyphs and toolbar icons use. Direct CreateTexture +
    // UpdateTexture with SDL_PIXELFORMAT_RGBA32 produced inverted/garbage
    // glyphs on some SDL_Renderer backends (desktop + KMS).
    SDL_Surface* surf = SDL_CreateRGBSurfaceFrom(
        (void*)pixels, width, height, 32, 4 * width,
        0x000000ff, 0x0000ff00, 0x00ff0000, 0xff000000);
    if (surf == nullptr)
    {
        SDL_Log("error creating font surface");
        return false;
    }
    bd->FontTexture = SDL_CreateTextureFromSurface(bd->Renderer, surf);
    SDL_FreeSurface(surf);
    if (bd->FontTexture == nullptr)
    {
        SDL_Log("error creating font texture");
        return false;
    }
    SDL_SetTextureBlendMode(bd->FontTexture, SDL_BLENDMODE_BLEND);
    SDL_SetTextureColorMod(bd->FontTexture, 255, 255, 255);
    SDL_SetTextureAlphaMod(bd->FontTexture, 255);
#if SDL_VERSION_ATLEAST(2,0,12)
    SDL_SetTextureScaleMode(bd->FontTexture, SDL_ScaleModeLinear);
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
#if IMGUI_SDL_RENDERER_LEGACY
    ImGui_ImplSDLRenderer2_Data* bd = ImGui_ImplSDLRenderer2_GetBackendData();
    if (bd && bd->Cache)
    {
        SDL_DestroyTexture(bd->Cache);
        bd->Cache = nullptr;
        bd->CacheValid = false;
    }
#endif
}

//-----------------------------------------------------------------------------

#if defined(__clang__)
#pragma clang diagnostic pop
#endif

#endif // #ifndef IMGUI_DISABLE
