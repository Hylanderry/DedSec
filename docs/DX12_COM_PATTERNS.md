# DX12 COM Vtable em C — Notas de Debug

Esse documento é um registro dos problemas que encontrei implementando um pipeline D3D12 completo em C puro (sem wrappers C++), focado na ABI do Xbox Series. Tudo aqui veio de crash real e validação em hardware Turing wave32.

## Chamada COM Vtable

Em C, todo objeto D3D12 é uma interface COM com `lpVtbl` apontando pra tabela de funções. A convenção de chamada passa o ponteiro do objeto como primeiro argumento (o `this` do C++):

```c
ID3D12Device* device = NULL;
HRESULT hr = D3D12CreateDevice(NULL, D3D_FEATURE_LEVEL_12_0, 
    &IID_ID3D12Device, (void**)&device);

hr = device->lpVtbl->CreateCommandQueue(device, &queue_desc, 
    &IID_ID3D12CommandQueue, (void**)&cmd_queue);
```

## GetCPUDescriptorHandleForHeapStart — Cuidado

Essa função retorna struct por valor mas na ABI C usa out-parameter. Errar isso corrompe a stack:

```c
// CERTO — dois parâmetros com ponteiro de saída
D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle;
heap->lpVtbl->GetCPUDescriptorHandleForHeapStart(heap, &cpu_handle);

// ERRADO — retorno por valor direto (stack corruption silenciosa)
D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle = 
    heap->lpVtbl->GetCPUDescriptorHandleForHeapStart(heap);
```

Isso vale pra todos os métodos de descriptor handle no binding C.

## Reset com SEH

Access violation silencioso no Reset não gera HRESULT de falha. SEH pega:

```c
__try {
    HRESULT hr_alloc = cmd_alloc->lpVtbl->Reset(cmd_alloc);
    if (FAILED(hr_alloc)) {
        log_error("cmd_alloc->Reset FAILED: 0x%08X", hr_alloc);
        continue;
    }
    
    HRESULT hr_list = cmd_list->lpVtbl->Reset(cmd_list, cmd_alloc, NULL);
    if (FAILED(hr_list)) {
        log_error("cmd_list->Reset FAILED: 0x%08X", hr_list);
        continue;
    }
} __except (EXCEPTION_EXECUTE_HANDLER) {
    log_error("SEH EXCEPTION in Reset: 0x%08X", GetExceptionCode());
    continue;
}
```

Sem o `__try/__except`, o processo morre sem log nenhum quando o command allocator está em estado inválido.

## State Tracking de Back Buffer

Depois do `Present()`, back buffers ficam em `PRESENT`. Antes de renderizar, transiciona pra `RENDER_TARGET`. Depois de renderizar, volta pra `PRESENT`:

```c
// Antes de renderizar
D3D12_RESOURCE_BARRIER barrier = {0};
barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
barrier.Transition.pResource = back_buffers[frame_idx];
barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
cmd_list->lpVtbl->ResourceBarrier(cmd_list, 1, &barrier);

// ... render ...

// Depois de renderizar (antes do Present)
barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
cmd_list->lpVtbl->ResourceBarrier(cmd_list, 1, &barrier);
```

## Fence Sync

```c
UINT64 fence_value = 1;
cmd_queue->lpVtbl->Signal(cmd_queue, fence, fence_value);

if (fence->lpVtbl->GetCompletedValue(fence) < fence_value) {
    fence->lpVtbl->SetEventOnCompletion(fence, fence_value, fence_event);
    WaitForSingleObject(fence_event, INFINITE);
}
fence_value++;
```

## Upload Buffer

Upload buffers começam em `GENERIC_READ`. Depois do `CopyTextureRegion`, não precisa transicionar estado pra cópias subsequentes no mesmo command list — o estado é rastreado pelo command allocator, não pelo resource.

## Logging Step-by-Step pra Pegar Crash Silencioso

HRESULT check sozinho não pega tudo. Log com `fflush(NULL)` depois de cada passo identifica onde exatamente o processo morre:

```c
log_msg("[RENDER] Step 1: GetBackBufferIndex..."); fflush(NULL);
frame_idx = swapchain->lpVtbl->GetCurrentBackBufferIndex(swapchain);

log_msg("[RENDER] Step 2: frame_idx=%u, Reset...", frame_idx); fflush(NULL);
// ... Reset calls ...

log_msg("[RENDER] Step 3: ClearRTV done"); fflush(NULL);
// ... rendering ...

log_msg("[RENDER] Step 7: ExecuteCommandLists + Present..."); fflush(NULL);
```

Foi assim que identifiquei crashes entre passos que o HRESULT não reportava. Sem o flush, o log bufferizado perde as últimas linhas antes do crash.