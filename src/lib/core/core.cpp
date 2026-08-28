#include "core.hpp"
#include "SDL3/SDL_init.h"
#include "SDL3/SDL_properties.h"
#include "SDL3/SDL_video.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <combaseapi.h>
#include <d3dcommon.h>
#include <d3dcompiler.h>
#include <directx/d3dx12_core.h>
#include <directx/d3dx12_default.h>
#include <directx/d3dx12_root_signature.h>
#include <dxgi.h>
#include <dxgi1_3.h>
#include <dxgiformat.h>
#include <minwindef.h>
#include <stdlib.h>
#include <wrl/client.h>

namespace KR {

Core::Core() {
  createWindow();
  createComFactory();
  enableDebug();
  createDevice();
  createCommandQueue();
  createSwapchain();
  createDescriptorHeaps();
  createFrameRes();
  createAllocator();
  loadAssets();
};

void Core::createWindow() {
  SDL_Init(SDL_INIT_VIDEO);
  window = SDL_CreateWindow("App", width, height, SDL_WINDOW_RESIZABLE);
  auto properties = SDL_GetWindowProperties(window);
  hwnd = (HWND)SDL_GetPointerProperty(
      properties, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
};
void Core::createComFactory() { CreateDXGIFactory1(IID_PPV_ARGS(&comfactory)); }

void Core::enableDebug() {
#if defined(_DEBUG)
  Microsoft::WRL::ComPtr<ID3D12Debug> debug;
  if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) {
    debug->EnableDebugLayer();
  };
#endif
}

void Core::createDevice() {
  D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device));
}

void Core::createCommandQueue() {
  D3D12_COMMAND_QUEUE_DESC desc = {};
  desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
  desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
  device->CreateCommandQueue(&desc, IID_PPV_ARGS(&queue));
};

void Core::createSwapchain() {
  Microsoft::WRL::ComPtr<IDXGISwapChain1> s;
  DXGI_SWAP_CHAIN_DESC1 desc = {};
  desc.BufferCount = framecount;
  desc.Width = width;
  desc.Height = height;
  desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
  desc.SampleDesc.Count = 1;
  comfactory->CreateSwapChainForHwnd(queue.Get(), hwnd, &desc, nullptr, nullptr,
                                     &s);
  comfactory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);

  s.As(&swapchain);
  frameindex = swapchain->GetCurrentBackBufferIndex();
};

void Core::createDescriptorHeaps() {
  D3D12_DESCRIPTOR_HEAP_DESC desc = {};
  desc.NumDescriptors = framecount;
  desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
  desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
  device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&rtvHeap));
  rtvSize =
      device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
};

void Core::createFrameRes() {
  CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(
      rtvHeap->GetCPUDescriptorHandleForHeapStart());
  renderTargets.resize(framecount);
  for (int n = 0; n < framecount; n++) {
    swapchain->GetBuffer(n, IID_PPV_ARGS(&renderTargets.at(n)));
    device->CreateRenderTargetView(renderTargets.at(n).Get(), nullptr,
                                   rtvHandle);
    rtvHandle.Offset(1, rtvSize);
  }
};
void Core::createAllocator() {
  device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                 IID_PPV_ARGS(&allocator));
};
void Core::createRootSignature() {
  CD3DX12_ROOT_SIGNATURE_DESC desc;
  desc.Init(0, nullptr, 0, nullptr,
            D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
  Microsoft::WRL::ComPtr<ID3DBlob> signature;
  Microsoft::WRL::ComPtr<ID3DBlob> error;
  D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &signature,
                              &error);
  device->CreateRootSignature(0, signature->GetBufferPointer(),
                              signature->GetBufferSize(), IID_PPV_ARGS(&sig));
}
void Core::createPipelineState() {
  UINT compile_flags = 0;
  Microsoft::WRL::ComPtr<ID3DBlob> vertexShader;
  Microsoft::WRL::ComPtr<ID3DBlob> pixelShader;
  D3DCompileFromFile(L"assets/default_shader.hlsl", nullptr, nullptr, "VSMain",
                     "vs_5_0", compile_flags, 0, &vertexShader, nullptr);
  D3DCompileFromFile(L"assets/default_shader.hlsl", nullptr, nullptr, "PSMain",
                     "ps_5_0", compile_flags, 0, &pixelShader, nullptr);
  D3D12_INPUT_ELEMENT_DESC descs[] = {
      {"POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0,
       D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
      {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12,
       D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}};
  D3D12_GRAPHICS_PIPELINE_STATE_DESC ppsdesc = {};
  ppsdesc.InputLayout = {descs, _countof(descs)};
  ppsdesc.pRootSignature = sig.Get();
  ppsdesc.VS = {reinterpret_cast<UINT8 *>(vertexShader->GetBufferPointer()),
                vertexShader->GetBufferSize()};
  ppsdesc.PS = {reinterpret_cast<UINT8 *>(pixelShader->GetBufferPointer()),
                pixelShader->GetBufferSize()};
  ppsdesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
  ppsdesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
  ppsdesc.DepthStencilState.DepthEnable = false;
  ppsdesc.DepthStencilState.StencilEnable = false;
  ppsdesc.SampleMask = UINT_MAX;
  ppsdesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  ppsdesc.NumRenderTargets = 1;
  ppsdesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
  ppsdesc.SampleDesc.Count = 1;
  device->CreateGraphicsPipelineState(&ppsdesc, IID_PPV_ARGS(&state));
};

void Core::createCommandList() {
  device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(),
                            state.Get(), IID_PPV_ARGS(&commandList));
  commandList->Close();
};

void Core::loadAssets() {
  createRootSignature();
  createPipelineState();
  createCommandList();
}
} // namespace KR
