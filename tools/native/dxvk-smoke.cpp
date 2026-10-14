// dxvk-smoke.cpp - the smallest program that uses DXVK's native D3D9 (libdxvk_d3d9.so, SDL2 window system): creates
// a device on an SDL Vulkan window, clears it to a color for a number of frames, and says what adapter it got.
// For checking that DXVK and a Vulkan driver work on a machine before the engine is involved.
//
//   clang++ -std=c++17 dxvk-smoke.cpp -I<dxvk>/include/native/directx -I<dxvk>/include/native/windows \
//       -L<dxvk build>/src/d3d9 -ldxvk_d3d9 -lSDL2 -o dxvk-smoke
#include <SDL2/SDL.h>
#include <stdio.h>

#include <d3d9.h>

int main(int argc, char **argv)
{
    const int frames = argc > 1 ? atoi(argv[1]) : 120;
    if (SDL_Init(SDL_INIT_VIDEO) != 0)
    {
        printf("SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    SDL_Window *window = SDL_CreateWindow("dxvk smoke", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720,
        SDL_WINDOW_VULKAN | SDL_WINDOW_SHOWN);
    if (!window)
    {
        printf("SDL_CreateWindow(VULKAN): %s\n", SDL_GetError());
        return 1;
    }
    IDirect3D9 *d3d = Direct3DCreate9(D3D_SDK_VERSION);
    if (!d3d)
    {
        printf("Direct3DCreate9 failed\n");
        return 1;
    }
    D3DADAPTER_IDENTIFIER9 id;
    if (SUCCEEDED(d3d->GetAdapterIdentifier(0, 0, &id)))
        printf("adapter: %s (%s)\n", id.Description, id.Driver);
    D3DPRESENT_PARAMETERS pp = {};
    pp.BackBufferWidth = 1280;
    pp.BackBufferHeight = 720;
    pp.BackBufferFormat = D3DFMT_A8R8G8B8;
    pp.BackBufferCount = 1;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.hDeviceWindow = (HWND)window;
    pp.Windowed = TRUE;
    pp.EnableAutoDepthStencil = TRUE;
    pp.AutoDepthStencilFormat = D3DFMT_D24S8;
    pp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
    IDirect3DDevice9 *device = nullptr;
    const HRESULT hr = d3d->CreateDevice(0, D3DDEVTYPE_HAL, (HWND)window, D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &device);
    if (FAILED(hr) || !device)
    {
        printf("CreateDevice failed: 0x%08x\n", (unsigned)hr);
        return 1;
    }
    const Uint32 start = SDL_GetTicks();
    for (int i = 0; i < frames; ++i)
    {
        SDL_Event e;
        while (SDL_PollEvent(&e))
        {
        }
        device->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, D3DCOLOR_XRGB(20, (i * 2) & 255, 160), 1.0f, 0);
        device->BeginScene();
        device->EndScene();
        device->Present(nullptr, nullptr, nullptr, nullptr);
    }
    printf("presented %d frames in %u ms\n", frames, (unsigned)(SDL_GetTicks() - start));
    device->Release();
    d3d->Release();
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
