# DedSec Boot Roadmap — Objetivo: Menu Principal do GTA VI

> **Meta:** Chegar ao menu principal do jogo no PC (GTX 1650 4GB)
> **Referência:** [XCL](https://github.com/ivall/xcl) — Rust, 486 commits, jogos retail rodando
> **Última atualização:** 2026-11-24

---

## Status Atual do DedSec (branch `dedsec-clean`)

### ✅ Implementado e Funcional
| Componente | Arquivos | Status |
|---|---|---|
| PE Loader + TLS /GA patch | `src/pe_loader.cpp`, `src/main.cpp` | Boot do exe retail |
| D3D12.X Device Proxy (vtable) | `shims/d3d12_xs_device.cpp` | Slots 0-82 mapeados |
| Frame Clock (slots 79-80) | `src/dedsec_frame_clock.cpp` | Multi-type fire, lazy init |
| CPU Writes Bottom-of-Pipe (81-82) | `src/dedsec_cpu_writes.cpp` | Per-queue fence, ring buffer |
| RDNA2→HLSL Translator | `shims/rdna2_*.cpp` (10 arquivos) | 1.630 formas ISA cobertas |
| DXC Runtime + Cache | `shims/dedsec_dxc_*.cpp` | Dynamic load, memory gate |
| XBCH Parser + Async PSO | `src/xbch_parser.cpp`, `shims/dedsec_async_pso.cpp` | Framework P0 |
| Command List IR (Draw/Dispatch/PSO) | `src/command_list.h` | IR definido |
| XGameRuntime Shim | `shims/xgameruntime.cpp` | XErrorReport export |
| XAudio2 Shim | `shims/xaudio2_shim.cpp` | Stub funcional |
| Xbox Input Adapter | `shims/xbox_input_adapter.cpp` | Gamepad mapping |
| XMem Shim | `shims/xmem.cpp` | Alloc/free básico |
| GDeflate Decompressor | `shims/gdeflate_decompressor.cpp` | Asset decompression |
| FSR3 Hook | `shims/dedsec_fsr3*.cpp` | Upscaling hook |
| GPU Tier Detection | `shims/dedsec_gpu_tier.cpp` | GTX 1650 profile |
| Launcher Web | `launcher/index.html` | UI minimalista |
| Test Suite | `runtime/test/` | 19/20 passando |

### ❌ Falta para Boot do Menu (Bloqueantes Críticos)

| # | Componente | Prioridade | Descrição | Ref XCL |
|---|---|---|---|---|
| 1 | **kernelx.dll completo** | 🔴 P0 | Memory management (VirtualAlloc/Free/MapPhysicalPages), TlsAlloc/GetValue, CreateEvent/Semaphore, thread primitives. Sem isso o exe crasha antes de chegar no main(). | `hle/kernelx/` |
| 2 | **VFS / RPF9 Reader** | 🔴 P0 | Leitura de archives RPF9 do GTA VI. O jogo não carrega NENHUM asset sem isso. | `rpf.rs`, `vfs.rs` |
| 3 | **D3D12.X Command List Replay** | 🔴 P0 | Gravação e replay de comandos GPU via ID3D12GraphicsCommandList. Sem isso = tela preta. | `gpu/xbox.rs`, `hle/d3d12x/` |
| 4 | **UMA Allocator** | 🔴 P0 | Gerenciamento de memória unificada CPU/GPU. O jogo espera alocações contíguas que o PC não fornece nativamente. VRAM leak atual do XCL mostra como é crítico. | `gpu/uma.rs`, `gpu/pack.rs` |
| 5 | **Texture Tiling / Swizzle** | 🟡 P1 | Decodificação de texturas GFX10 com tiling Xbox. Sem isso = texturas pretas/corrompidas no menu. | `gpu/tiling.rs`, `gpu/texture.rs` |
| 6 | **Descriptor Heap Management** | 🟡 P1 | Criação e binding de CBV/SRV/UAV heaps. O menu precisa de descriptors para renderizar UI. | `gpu/desc.rs` |
| 7 | **PresentX / Swap Chain** | 🟡 P1 | Apresentação de frames via IDXGISwapChain. Sem isso = nada na tela mesmo com render OK. | Issues #16 do XCL |
| 8 | **Root Signature Translation** | 🟡 P1 | Conversão de root signatures Xbox para PC. Já temos parser mas falta tradução completa. | `gpu/shader.rs` |
| 9 | **Bink 2 Video Decoder** | 🟢 P2 | Cutscenes e vídeos do menu. O jogo pode travar se não conseguir decodificar intro. | README XCL |
| 10 | **DirectStorage / Async IO** | 🟢 P2 | Streaming de assets. Sem isso o menu carrega mas o mundo não streama. | `hle/dstorage.rs` |
| 11 | **XMA2 Audio Decoder** | 🟢 P2 | Áudio do jogo. XAudio2 shim existe mas decoder XMA2 falta. | `hle/xma2/` |
| 12 | **NetOff / Network Stub** | ⚪ P3 | Rede. Não necessário para menu single-player. | `hle/netoff.rs` |

### ⚠️ Problemas Conhecidos do XCL (Evitar Repetir)
- **VRAM leak**: Texturas copiadas por placement, nunca liberadas → OOM em placas 4GB (#2)
- **Null write GTA VI**: Crash em +0x2215c35 no RTX 4070 Ti (#4)
- **NVIDIA stall**: nvgpucomp64 trava em shaders grandes traduzidos (#10)
- **Frame clock race**: Handle race condition no frame pacing (#16)
- **Helper lanes dropped**: Ballot/WQM perde lanes em pixel shaders (#14)

---

## Plano de Execução Imediata

### Fase 1: Boot até main() (Semana 1-2)
1. Completar kernelx.dll — memory + sync primitives
2. VFS/RPF9 reader mínimo (ler pack names + file entries)
3. Validar que o exe chega em main() sem crash

### Fase 2: Tela Preta → Primeiro Frame (Semana 3-4)
4. D3D12.X command list replay funcional
5. UMA allocator básico (fixed-size arena)
6. Descriptor heap creation + binding
7. PresentX swap chain

### Fase 3: Menu Renderizado (Semana 5-6)
8. Texture tiling/swizzle GFX10
9. Root signature translation completa
10. Bink 2 stub (skip videos)
11. Validação visual do menu

### Fase 4: Jogabilidade Básica (Semana 7+)
12. DirectStorage async IO
13. XMA2 audio
14. Otimização VRAM para GTX 1650
15. Shader cache persistente

---

## Métricas de Progresso

| Marco | Status | Data |
|---|---|---|
| PE Loader boot | ✅ Done | 2026-11-20 |
| D3D12.X proxy vtable | ✅ Done | 2026-11-23 |
| Frame clock + CPU writes | ✅ Done | 2026-11-24 |
| kernelx.dll completo | ❌ TODO | — |
| VFS/RPF9 reader | ❌ TODO | — |
| Command list replay | ❌ TODO | — |
| Primeiro frame na tela | ❌ TODO | — |
| Menu principal visível | ❌ TODO | — |
| Gameplay básico | ❌ TODO | — |