# DedSec Roadmap — GTA VI Xbox → PC Port

> **Objetivo**: Boot completo do GTA VI (retail) em PC via DX12 nativo, otimizado para GTX 1650 4GB.
> **Referência**: [XCL](https://github.com/ivall/xcl) (Rust, 486 commits, retail games run)
> **Stack**: C++ / MSVC x64 / D3D12 / HLSL
> **Branch**: `dedsec-clean` @ [Hylanderry/DedSec](https://github.com/Hylanderry/DedSec)

---

## Status Atual (2026-11-23)

### ✅ Componentes Completos

| # | Componente | Arquivo(s) | Status |
|---|-----------|------------|--------|
| 1 | PE Loader + TLS /GA Patch | `src/pe_loader.cpp` | ✅ Funcional, testes passando |
| 2 | RDNA2 → HLSL Shader Translator | `shims/rdna2_*.cpp` (9 arquivos) | ✅ 1.630/1.630 formas gfx1030 (100%) |
| 3 | D3D12.X Device Vtable Proxy | `shims/d3d12_xs_device.cpp` | ✅ Slots 0-2, 14, 62, 76, 79-82 |
| 4 | Frame Clock (ScheduleFrameEventX/WaitFrameEventX) | `src/dedsec_frame_clock.{h,cpp}` | ✅ Multi-type fire, lazy init |
| 5 | CPU Writes Bottom-of-Pipe | `src/dedsec_cpu_writes.{h,cpp}` | ✅ Per-queue fences, ring buffer |
| 6 | XErrorReport Export | `shims/xgameruntime.cpp` | ✅ Previne int3 breakpoint |
| 7 | XGameRuntime Shim | `shims/xgameruntime.cpp` | ✅ Thunks básicos |
| 8 | XMem Shim | `shims/xmem.cpp` | ✅ Alocação básica |
| 9 | XBCH Parser + Async PSO Framework | `src/xbch_parser.cpp`, `shims/dedsec_async_pso.cpp` | ✅ P0 |
| 10 | Command List IR (Draw/Dispatch/PSO) | `src/command_list.h` | ✅ P0 |
| 11 | Root Signature Oracle | `src/root_signature.cpp` | ✅ Testes passando |
| 12 | GFX10 Descriptor Decoding | `src/gfx10_descriptors.h` | ✅ Testes passando |
| 13 | DXC Runtime (dynamic load + cache) | `shims/dedsec_dxc_runtime.cpp` | ✅ |
| 14 | GPU Tier Detection | `shims/dedsec_gpu_tier.cpp` | ✅ |
| 15 | FSR3 Hook | `shims/dedsec_fsr3_hook.cpp` | ✅ Stub |
| 16 | Xbox Input Adapter | `shims/xbox_input_adapter.cpp` | ✅ Básico |
| 17 | XAudio2 Shim | `shims/xaudio2_shim.cpp` | ✅ Básico |
| 18 | GDeflate Decompressor | `shims/gdeflate_decompressor.cpp` | ✅ |
| 19 | Launcher (HTML/CSS/JS) | `launcher/index.html` | ✅ GTX 1650 defaults |

**Testes**: 19/20 passando (rdna2_mimg tem C2110 pré-existente)

### ❌ Componentes que Faltam para Boot

Priorizados por ordem de bloqueio — cada nível depende do anterior:

#### Nível 0: Bloqueadores Absolutos (sem isso nem chega no main())

| # | Componente | Equivalente XCL | Prioridade | Esforço |
|---|-----------|-----------------|------------|---------|
| A1 | **kernelx.dll completo** (memory management, threads, sync) | `hle/kernelx/` + `hle/kernelx.rs` | 🔴 CRÍTICO | Alto |
| A2 | **VFS / RPF9 Reader** (sistema de arquivos virtualizado) | `rpf.rs` + `vfs.rs` + `vfs_hooks.rs` | 🔴 CRÍTICO | Alto |
| A3 | **D3D12.X Command List Replay** (graphics + compute) | `hle/d3d12x/` | 🔴 CRÍTICO | Muito Alto |

#### Nível 1: Bloqueadores de Render (chega no main(), tela preta)

| # | Componente | Equivalente XCL | Prioridade | Esforço |
|---|-----------|-----------------|------------|---------|
| B1 | **UMA Allocator** (shared CPU/GPU memory) | `gpu/uma.rs` + `gpu/pack.rs` | 🟠 ALTO | Médio |
| B2 | **Texture Tiling / Swizzling** (GFX10 surface decode) | `gpu/tiling.rs` + `gpu/tiling_tables.rs` | 🟠 ALTO | Alto |
| B3 | **D3D12.X Presentation** (PresentX, swap chain) | Issues #16 (frame pacing) | 🟠 ALTO | Médio |
| B4 | **Descriptor Heap Management** (bindless / CBV_SRV_UAV) | `gpu/desc.rs` | 🟠 ALTO | Médio |

#### Nível 2: Bloqueadores de Gameplay (renderiza mas não joga)

| # | Componente | Equivalente XCL | Prioridade | Esforço |
|---|-----------|-----------------|------------|---------|
| C1 | **GameInput / KBM Mapping** | `hle/gameinput.rs` + `hle/kbm.rs` | 🟡 MÉDIO | Baixo |
| C2 | **XAudio2 Completo** (decoding + mixing) | `hle/xaudio2.rs` + `hle/xma2/` | 🟡 MÉDIO | Médio |
| C3 | **User Profile / Save System** | `hle/user.rs` | 🟡 MÉDIO | Baixo |
| C4 | **Bink 2 Video Decoder** | README menciona suporte | 🟡 MÉDIO | Médio |

#### Nível 3: Polish e Estabilidade

| # | Componente | Equivalente XCL | Prioridade | Esforço |
|---|-----------|-----------------|------------|---------|
| D1 | **Ray Tracing Emulation** | `gpu/bvh.rs` | 🟢 BAIXO | Muito Alto |
| D2 | **DirectStorage** | `hle/dstorage.rs` | 🟢 BAIXO | Alto |
| D3 | **Network Offload** | `hle/netoff.rs` | 🟢 BAIXO | Médio |
| D4 | **Media Foundation / Opus** | `hle/mfplat.rs` + `hle/opus.rs` | 🟢 BAIXO | Médio |
| D5 | **ACP HAL** | `hle/acphal.rs` | 🟢 BAIXO | Baixo |

---

## Análise XCL — Lições Aprendidas

### O que o XCL faz bem (e devemos replicar)
- **Lazy subsystem init**: threads só criadas quando necessárias (evita teardown crashes)
- **Per-queue fences**: sincronização correta GPU→CPU sem busy-wait
- **Ring buffers fixos**: zero alocação por frame, VRAM-conscious
- **Multi-type frame events**: dispara todos os tipos no mesmo deadline
- **Shader recompiler com zero erros**: cobertura total do ISA RDNA2

### Problemas conhecidos do XCL (devemos evitar)
- **VRAM leak**: texturas copiadas por placement, nunca liberadas (Issue #2)
- **Null write em GTA VI**: +0x2215c35 em RTX 4070 Ti (Issue #4)
- **Black image**: physical pages esgotam GPU windows (Issue #7)
- **NVIDIA stall**: nvgpucomp64 trava em shaders grandes traduzidos (Issue #10)
- **Overlapping VRAM arenas**: arenas se sobrepõem (Issue #15)
- **Frame clock handle race**: condição de corrida nos handles (Issue #16)

### Nossa vantagem sobre o XCL
- **C++ nativo**: sem overhead de FFI Rust↔C, integração direta com D3D12
- **Foco GTX 1650 4GB**: budget de VRAM definido desde o início
- **Código limpo**: fork dedicado, sem dívida técnica do Leonida
- **Design VRAM-first**: ring buffers, stack arrays, zero heap per-frame

---

## Plano de Execução Imediata

### Sprint 1: Kernel + VFS (Nível 0A)
1. Implementar `kernelx.dll` shim com memory management (VirtualAlloc wrappers, TLS, critical sections)
2. Implementar RPF9 reader básico (header parse, entry lookup, decompression)
3. Implementar VFS layer que mapeia paths do jogo para arquivos locais
4. **Meta**: loader consegue resolver imports do kernelx e ler arquivos do jogo

### Sprint 2: D3D12.X Command List Replay (Nível 0B)
1. Expandir Command List IR para cobrir todos os comandos gráficos
2. Implementar replay engine que traduz Xbox command lists → PC D3D12
3. Wire up PSO creation pipeline completo (graphics + compute)
4. **Meta**: primeiro frame renderizado (mesmo que preto)

### Sprint 3: Memory + Textures (Nível 1)
1. UMA allocator com budget tracking para 4GB
2. Texture tiling/swizzling decoder para GFX10 surfaces
3. Descriptor heap manager com bindless support
4. PresentX → DXGI Present bridge
5. **Meta**: menu principal visível

### Sprint 4: Input + Audio + Gameplay (Nível 2)
1. GameInput → XInput/RawInput mapping
2. XAudio2 decoding pipeline completo
3. User profile stub
4. **Meta**: gameplay funcional

---

## Métricas de Progresso

| Métrica | Atual | Meta Sprint 1 | Meta Sprint 2 | Meta Boot |
|---------|-------|---------------|---------------|-----------|
| Componentes completos | 19 | 22 | 25 | 30+ |
| Testes passando | 19/20 | 22/23 | 25/26 | 30+/30 |
| Shaders traduzidos | 1.630 | 1.630 | 1.630 | 1.630 |
| VRAM peak (menu) | N/A | <512MB | <1GB | <2GB |
| FPS (menu, 720p) | N/A | N/A | >10 | >30 |

---

*Última atualização: 2026-11-23 | Commit: b51b9bb8*