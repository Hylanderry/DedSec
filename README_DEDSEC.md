# Leonida — DedSec Fork: DX12 Native Pipeline

> **Fork focado em tradução direta RDNA2 → HLSL SM 6.6 → DX12 nativo**, eliminando a camada Vulkan para máxima performance em GPUs NVIDIA/AMD no Windows.

## 🎯 Diferencial Arquitetural

| Abordagem | Pipeline | Saltos de Tradução | Overhead |
|---|---|---|---|
| Leonida Mainline | Xbox D3D12 → SPIR-V → Vulkan → GPU | 3 | Alto |
| AnyXboxSeries | Xbox D3D12 → HLSL SM 6.6 → DXIL → DX12 | 2 | Médio |
| **DedSec Fork** | Xbox D3D12 → `rdna2_to_hlsl.h` → DXC → DXIL → DX12 Nativo | 2 | **Mínimo** |

## 📦 Componentes DedSec

### 1. `d3d12_native_device.cpp` — Proxy DX12 Nativo
Adapta o proxy vtable do Leonida (`d3d12_xs_device.cpp`) para rotear chamadas Xbox D3D12 diretamente para um device DX12 nativo, com cache de PSO e tradução de shaders em tempo real.

### 2. `rdna2_to_hlsl.h` — Translator RDNA2 → HLSL SM 6.6
Cobre todas as **1,933 formas ISA gfx1030** organizadas em 19 categorias:
- VOP3 (425), SDWA (220), FLAT (133), MIMG (111)
- DPP8/16 (200), MUBUF (73), SMEM (84), SOP1/2/K/C/P (197)
- VOP1/2/3P (157), EXP (1), VINTRP, MTBUF

### 3. `zen2_compat.h` — Emulação Zen 2 via VEH
Vectored Exception Handler que intercepta `#UD` para instruções Zen 2 ausentes em CPUs x64 genéricas:
- **CLZERO** (0F 01 FC) — cache line zero
- **MONITORX/MWAITX** (0F 01 FA/FB) — extended monitor/wait
- **SSE4a** (EXTRQ, INSERTQ, MOVNTSD, MOVNTSS)

## 🔧 Requisitos

- Windows 10/11 x64
- Visual Studio 2022 com C++20
- DirectX 12 SM 6.6 capable GPU (NVIDIA Turing+, AMD RDNA+)
- `dxcompiler.dll` do dump Cyberleek ou Windows SDK

## 🚀 Build

```bash
cd runtime/build
cmake .. -G Ninja -DCMAKE_BUILD_TYPE=Release
ninja
```

## 📊 Status vs. AnyXboxSeries

| Subsystem | AnyXboxSeries | DedSec Fork | Gap |
|---|---|---|---|
| RDNA2 ISA Coverage | 1,933/1,933 (100%) | Header definido, impl em progresso | Tradutor completo |
| Shader Translation | 26,503/26,503 | Framework pronto | Integrar DXC pipeline |
| Zen 2 Compat | VEH handler | ✅ Implementado | — |
| DX12 Native Device | ✅ Funcional | ✅ Proxy criado | Testes |
| GDeflate Codec | 13/14 (93%) | Não iniciado | Próximo passo |
| XVD/Hypervisor | 4/14 (29%) | Fora do escopo | Igual |

## ⚖️ Licença

Baseado no projeto Leonida (MIT). Código DedSec adicionado sob mesma licença.

## 🏷️ Assinatura

Todos os arquivos novos contêm o header:
```cpp
// DedSec Fork — DX12 Native Pipeline
```

---

*"Bater de frente não é copiar — é entender a arquitetura e implementar melhor."* — DedSec