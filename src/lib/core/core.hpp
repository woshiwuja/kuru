#pragma once
#include "SDL3/SDL_video.h"
#include <combaseapi.h>
#include <cstdint>
#include <directx/d3dx12.h>
#include <directx/d3d12sdklayers.h>
#include <dxgi1_4.h>
#include <intsafe.h>
#include <wrl.h>
#include <wrl/client.h>
#include <vector>
#include "../common/common.hpp"
namespace KR {
    struct Core {
        D3D12_VIEWPORT viewport;
        D3D12_RECT scissor;
        Microsoft::WRL::ComPtr<IDXGISwapChain3> swapchain = nullptr;
        Microsoft::WRL::ComPtr<ID3D12Device> device = nullptr;
        std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> renderTargets;
        Microsoft::WRL::ComPtr<ID3D12CommandAllocator> allocator= nullptr;
        Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue = nullptr;
        Microsoft::WRL::ComPtr<ID3D12RootSignature> sig = nullptr;
        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvHeap= nullptr;
        Microsoft::WRL::ComPtr<ID3D12PipelineState> state = nullptr;
        Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> commandList = nullptr;
        Microsoft::WRL::ComPtr<IDXGIFactory4> comfactory = nullptr;
        UINT rtvSize;
        SDL_Window* window = nullptr;
        HWND hwnd = nullptr;
        uint16_t framecount = 3;
        uint16_t width = 800;
        uint16_t height = 600;
        uint64_t frameindex=0;
        bool useWarp = false;
        std::vector<vec3> vertexBuffer;

        Core();
        void createComFactory();
        void createWindow();
        void enableDebug();
        void createDevice();
        void createCommandQueue();
        void createSwapchain();
        void createDescriptorHeaps();
        void createFrameRes();
        void createAllocator();
        void createRootSignature();
        void createPipelineState();
        void createCommandList();
        void loadAssets();
    };
}
