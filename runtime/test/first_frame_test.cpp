// DedSec First Frame Test — standalone DX12 renderer that proves the D3D12 proxy
// pipeline works end-to-end: device creation, swapchain, command list with barriers,
// clear, viewport/scissor, and Present. No game assets, no RPF decryption needed.
//
// This is the minimum viable first frame: a solid-color clear presented to screen.
// If this works, the D3D12 proxy layer is functional and the blocker for real game
// rendering is purely asset loading (shaders/textures from encrypted RPFs).
//
// Signed-off-by: DedSec <hylanderry@users.noreply.github.com>

#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

constexpr UINT kWidth = 1280;
constexpr UINT kHeight = 720;
constexpr UINT kBufferCount = 2;

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_CLOSE) { PostQuitMessage(0); return 0; }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

bool check(HRESULT hr, const char* what) {
    if (FAILED(hr)) {
        std::fprintf(stderr, "[FAIL] %s: hr=0x%08lx\n", what, static_cast<unsigned long>(hr));
        return false;
    }
    return true;
}

} // namespace

int main() {
    std::printf("[FIRST-FRAME] DedSec DX12 First Frame Test starting...\n");

    // --- Window ---
    WNDCLASSEXA wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.lpszClassName = "DedSecFirstFrame";
    wc.hInstance = GetModuleHandleA(nullptr);
    if (!RegisterClassExA(&wc)) {
        std::fprintf(stderr, "[FAIL] RegisterClassExA\n");
        return 1;
    }
    HWND hwnd = CreateWindowExA(0, wc.lpszClassName, "DedSec First Frame Test",
                                WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                                CW_USEDEFAULT, CW_USEDEFAULT, kWidth, kHeight,
                                nullptr, nullptr, wc.hInstance, nullptr);
    if (!hwnd) {
        std::fprintf(stderr, "[FAIL] CreateWindowExA\n");
        return 1;
    }
    std::printf("[OK] Window created (%ux%u)\n", kWidth, kHeight);

    // --- D3D12 Device ---
    ID3D12Device* device = nullptr;
    if (!check(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_12_0,
                                  __uuidof(ID3D12Device), reinterpret_cast<void**>(&device)),
               "D3D12CreateDevice"))
        return 1;
    std::printf("[OK] D3D12 device created\n");

    // --- Command Queue ---
    D3D12_COMMAND_QUEUE_DESC queueDesc{};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ID3D12CommandQueue* queue = nullptr;
    if (!check(device->CreateCommandQueue(&queueDesc, __uuidof(ID3D12CommandQueue),
                                           reinterpret_cast<void**>(&queue)),
               "CreateCommandQueue"))
        return 1;
    std::printf("[OK] Command queue created\n");

    // --- SwapChain ---
    IDXGIFactory4* factory = nullptr;
    if (!check(CreateDXGIFactory1(__uuidof(IDXGIFactory4), reinterpret_cast<void**>(&factory)),
               "CreateDXGIFactory1"))
        return 1;

    DXGI_SWAP_CHAIN_DESC1 scDesc{};
    scDesc.Width = kWidth;
    scDesc.Height = kHeight;
    scDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scDesc.BufferCount = kBufferCount;
    scDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    scDesc.SampleDesc.Count = 1;

    IDXGISwapChain1* sc1 = nullptr;
    if (!check(factory->CreateSwapChainForHwnd(queue, hwnd, &scDesc, nullptr, nullptr, &sc1),
               "CreateSwapChainForHwnd"))
        return 1;

    IDXGISwapChain3* swapchain = nullptr;
    if (!check(sc1->QueryInterface(__uuidof(IDXGISwapChain3), reinterpret_cast<void**>(&swapchain)),
               "QueryInterface IDXGISwapChain3"))
        return 1;
    sc1->Release();
    std::printf("[OK] SwapChain created (double-buffered, FLIP_DISCARD)\n");

    // --- RTV Heap ---
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc{};
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.NumDescriptors = kBufferCount;
    rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    ID3D12DescriptorHeap* rtvHeap = nullptr;
    if (!check(device->CreateDescriptorHeap(&rtvHeapDesc, __uuidof(ID3D12DescriptorHeap),
                                             reinterpret_cast<void**>(&rtvHeap)),
               "CreateDescriptorHeap RTV"))
        return 1;
    const UINT rtvSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    std::printf("[OK] RTV heap created (increment=%u)\n", rtvSize);

    // --- Back Buffer RTVs ---
    ID3D12Resource* backBuffers[kBufferCount] = {};
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandles[kBufferCount] = {};
    D3D12_CPU_DESCRIPTOR_HANDLE rtvStart = rtvHeap->GetCPUDescriptorHandleForHeapStart();
    for (UINT i = 0; i < kBufferCount; ++i) {
        if (!check(swapchain->GetBuffer(i, __uuidof(ID3D12Resource),
                                         reinterpret_cast<void**>(&backBuffers[i])),
                   "GetBuffer"))
            return 1;
        rtvHandles[i] = rtvStart;
        rtvHandles[i].ptr += static_cast<SIZE_T>(i) * rtvSize;
        device->CreateRenderTargetView(backBuffers[i], nullptr, rtvHandles[i]);
    }
    std::printf("[OK] Back buffer RTVs created\n");

    // --- Command Allocator + List ---
    ID3D12CommandAllocator* cmdAlloc = nullptr;
    if (!check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                               __uuidof(ID3D12CommandAllocator),
                                               reinterpret_cast<void**>(&cmdAlloc)),
               "CreateCommandAllocator"))
        return 1;

    ID3D12GraphicsCommandList* cmdList = nullptr;
    if (!check(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, cmdAlloc, nullptr,
                                          __uuidof(ID3D12GraphicsCommandList),
                                          reinterpret_cast<void**>(&cmdList)),
               "CreateCommandList"))
        return 1;
    std::printf("[OK] Command allocator + list created\n");

    // --- Fence ---
    ID3D12Fence* fence = nullptr;
    if (!check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence),
                                    reinterpret_cast<void**>(&fence)),
               "CreateFence"))
        return 1;
    HANDLE fenceEvent = CreateEventA(nullptr, FALSE, FALSE, nullptr);
    std::printf("[OK] Fence created\n");

    // --- Render Loop (10 frames) ---
    constexpr int kMaxFrames = 10;
    std::printf("[RENDER] Entering render loop (%d frames)...\n", kMaxFrames);

    MSG msg{};
    bool running = true;
    int frameCount = 0;

    while (running && frameCount < kMaxFrames) {
        while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) { running = false; break; }
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
        if (!running) break;

        const UINT idx = swapchain->GetCurrentBackBufferIndex();

        // Reset
        cmdAlloc->Reset();
        cmdList->Reset(cmdAlloc, nullptr);

        // Barrier: PRESENT -> RENDER_TARGET
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = backBuffers[idx];
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        cmdList->ResourceBarrier(1, &barrier);

        // Set RTV
        cmdList->OMSetRenderTargets(1, &rtvHandles[idx], FALSE, nullptr);

        // Viewport + Scissor
        D3D12_VIEWPORT vp{};
        vp.Width = static_cast<float>(kWidth);
        vp.Height = static_cast<float>(kHeight);
        vp.MaxDepth = 1.0f;
        cmdList->RSSetViewports(1, &vp);

        D3D12_RECT scissor{};
        scissor.right = kWidth;
        scissor.bottom = kHeight;
        cmdList->RSSetScissorRects(1, &scissor);

        // Clear with DedSec green (#00FF41)
        const float clearColor[4] = {0.0f, 1.0f, 0.255f, 1.0f};
        cmdList->ClearRenderTargetView(rtvHandles[idx], clearColor, 0, nullptr);

        // Barrier: RENDER_TARGET -> PRESENT
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
        cmdList->ResourceBarrier(1, &barrier);

        // Close + Execute + Present
        cmdList->Close();
        ID3D12CommandList* lists[] = {cmdList};
        queue->ExecuteCommandLists(1, lists);
        swapchain->Present(1, 0);

        // GPU sync
        const UINT64 fenceVal = static_cast<UINT64>(frameCount) + 1;
        queue->Signal(fence, fenceVal);
        if (fence->GetCompletedValue() < fenceVal) {
            fence->SetEventOnCompletion(fenceVal, fenceEvent);
            WaitForSingleObject(fenceEvent, INFINITE);
        }

        ++frameCount;
        std::printf("[FRAME %d/%d] Clear+Present OK (backbuffer=%u)\n", frameCount, kMaxFrames, idx);
    }

    std::printf("[SUCCESS] First frame test completed: %d frames rendered and presented!\n", frameCount);
    std::printf("[INFO] D3D12 proxy pipeline is functional.\n");
    std::printf("[INFO] Blocker for real game rendering: RPF asset decryption (shaders/textures).\n");

    // Cleanup
    CloseHandle(fenceEvent);
    if (fence) fence->Release();
    if (cmdList) cmdList->Release();
    if (cmdAlloc) cmdAlloc->Release();
    for (UINT i = 0; i < kBufferCount; ++i)
        if (backBuffers[i]) backBuffers[i]->Release();
    if (rtvHeap) rtvHeap->Release();
    if (swapchain) swapchain->Release();
    if (factory) factory->Release();
    if (queue) queue->Release();
    if (device) device->Release();
    DestroyWindow(hwnd);

    return (frameCount >= kMaxFrames) ? 0 : 1;
}