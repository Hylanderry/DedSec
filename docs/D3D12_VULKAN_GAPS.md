# Gaps no Shim D3D12→Vulkan do DedSec

Analisei o `d3d12_vulkan_device.cpp` (2414 linhas) e os shims relacionados. O que já funciona é sólido, mas tem buracos específicos que bloqueiam o boot. Aqui vai o que falta e como abordar.

## O que já funciona

- Device creation, queues, fences, descriptor heaps
- Committed e reserved resources
- RTV, DSV, SRV, UAV creation
- Samplers (bindless set)
- Barriers, clears, uploads, copies (incluindo BC)
- Color resolves

## O que falta pra renderizar algo

### 1. Pipeline State Objects (PSO)

Não existe `CreateGraphicsPipelineState` ou `CreateComputePipelineState`. Sem isso, zero draw calls.

O que precisa:
- Mapear `D3D12_GRAPHICS_PIPELINE_STATE_DESC` → `VkGraphicsPipelineCreateInfo`
- Root signature → `VkPipelineLayout` + descriptor set layouts
- Shader bytecode SPIR-V já está pronto (os 26.503 traduzidos), só falta wire up
- Blend state, rasterizer state, depth-stencil state → Vk equivalents
- Input layout → `VkVertexInputBindingDescription` + `VkVertexInputAttributeDescription`

Dica prática: começar com um PSO hardcoded mínimo (VS+PS, sem input assembly, triangle strip fullscreen) pra validar que o pipeline creation funciona antes de tentar mapear tudo.

### 2. Draw / Dispatch

Sem `DrawInstanced`, `DrawIndexedInstanced`, `Dispatch`. Esses são os métodos do command list que emitem trabalho real na GPU.

Mapeamento direto:
- `DrawInstanced(v, i, sv, si)` → `vkCmdDraw(cmd, v, i, sv, si)`
- `DrawIndexedInstanced(ic, i, sbv, sv, si)` → `vkCmdDrawIndexed(cmd, ic, i, sbv, sv, si)`
- `Dispatch(x, y, z)` → `vkCmdDispatch(cmd, x, y, z)`

Antes de cada draw: bind pipeline, bind descriptor sets, set viewport/scissor. O DedSec já tem viewport/scissor tracking mas não aplica porque não tem draw.

### 3. Root Signature Binding

O parser de root signature existe (`d3d12_xs_rootsig.cpp`) mas não conecta ao Vulkan pipeline layout. Cada root parameter precisa mapear pra um descriptor set binding:

- CBV → `VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER`
- SRV → `VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE` ou `VK_DESCRIPTOR_TYPE_STORAGE_BUFFER`
- UAV → `VK_DESCRIPTOR_TYPE_STORAGE_IMAGE` ou `VK_DESCRIPTOR_TYPE_STORAGE_BUFFER`
- Sampler → `VK_DESCRIPTOR_TYPE_SAMPLER`
- Root constants → push constants

O bindless set que já criaram ajuda, mas root signatures do GTA VI provavelmente usam uma mistura de root descriptors + descriptor tables. Precisa implementar os dois paths.

### 4. Swapchain (WSI)

Sem swapchain = sem Present. O shim atual não tem `IDXGISwapChain` implementation.

Implementar via `VK_KHR_swapchain`:
- `CreateSwapChainForHwnd` → `vkCreateSwapchainKHR` com `VkWin32SurfaceCreateInfoKHR`
- `Present` → `vkQueuePresentKHR` + acquire next image
- `GetCurrentBackBufferIndex` → track do acquire
- ResizeBuffers → recreate swapchain

Double buffering é suficiente. O fence sync pattern do DX12_COM_PATTERNS.md se aplica aqui também.

## Ordem sugerida de implementação

1. Swapchain WSI (sem isso nada aparece)
2. PSO mínimo hardcoded (valida pipeline creation)
3. DrawInstanced básico (valida draw path)
4. Root signature → pipeline layout (conecta shaders aos resources)
5. PSO completo com todos os states

Cada passo é testável isoladamente. O passo 1+2+3 já permite renderizar um triângulo colorido na tela, que é o "hello world" do port.

## Testes existentes que ajudam

O DedSec já tem `vk_pipeline_check.cpp` que valida criação de pipeline contra os 26.503 shaders traduzidos. Rodar isso numa GPU Turing wave32 e reportar falhas específicas seria contribuição imediata mesmo sem implementar nada novo.