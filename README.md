<p align="center">
  <img src="assets/dedsec_logo.png" alt="DedSec" width="200">
</p>

<h1 align="center"><b>DedSec</b></h1>

<p align="center">
  <b>Grand Theft Auto VI (Xbox Series) rodando nativamente no Windows via DX12.</b><br>
  Construído no seu PC, a partir da sua própria cópia.
</p>

<p align="center">
  <a href="#status-do-projeto">Status</a> •
  <a href="#como-compilar">Como compilar</a> •
  <a href="#como-funciona">Como funciona</a> •
  <a href="#quer-contribuir">Quer contribuir?</a> •
  <a href="#licença">Licença</a>
</p>

---

## O que é o DedSec

O DedSec é um projeto de recompilação estática que porta o GTA VI do Xbox Series S/X para Windows com pipeline **DX12 nativo**. O CPU do Xbox Series é x86-64 como o de um PC, então o código do jogo é carregado e executado nativamente, enquanto o software de sistema do console é reimplementado. Os programas de GPU RDNA2 pré-compilados do console são traduzidos diretamente para **HLSL SM 6.6 → DXIL → DX12** — dois saltos, sem intermediários.

> ⚠️ **Este repositório não contém o jogo, chaves ou executável.** Você precisa da sua própria cópia do Xbox Series (versão 1.0.2.1) com os arquivos já legíveis em disco. Nada aqui descriptografa ou contorna qualquer proteção. Tudo que o build gera fica no seu PC. Nunca compartilhe esses arquivos.

### Por que DX12 nativo?

| Pipeline | Saltos | Latência | Compatibilidade |
|----------|--------|----------|-----------------|
| **DedSec (DX12)** | RDNA2 → HLSL → DXIL → DX12 | **2** | Nativo Windows, máximo desempenho |
| Tradicional (Vulkan) | RDNA2 → SPIR-V → Vulkan → GPU | 3 | Camada extra de tradução |

A tradução direta para HLSL SM 6.6 elimina a camada SPIR-V/Vulkan, reduzindo overhead de driver e permitindo otimizações específicas do ecossistema DirectX: FSR3 Frame Generation nativo, Variable Rate Shading, PSO pre-warming, shader cache persistente, e async compute dispatch.

---

## Status do Projeto

**Fase inicial.** O jogo ainda não inicializa. Toda a infraestrutura que pode ser construída sem o executável do jogo está pronta.

| Componente | Status | Detalhes |
|------------|--------|----------|
| Verificação da cópia (Xbox 1.0.2.1) | ✅ Pronto | Detecção automática por tamanho/hash |
| Shaders: extração e desmontagem RDNA2 | ✅ 26.503 | LLVM gfx1013, todas as formas |
| Tradutor RDNA2 → HLSL SM 6.6 | ✅ Em progresso | VOP1/VOP2/VOP3/SDWA/DPP/MIMG completos; FLAT/MUBUF/SOP em andamento |
| MIMG (texturas): 111 formas | ✅ Completo | image_load/store/sample/gather/atomic/get_lod/resinfo |
| Compilador DXC Runtime | ✅ Completo | dxcompiler.dll dinâmico, HLSL→DXIL SM 6.6 |
| Adaptador de controles Xbox→PC | ✅ Completo | XInput + teclado fallback, deadzone radial/axial |
| FSR3 Frame Generation + Upscaling | ✅ Integrado | Hook DX12 nativo |
| GPU Tier Detection + Scalable Presets | ✅ Integrado | Detecção automática de hardware |
| Shader Cache Persistente | ✅ Integrado | FNV-1a hash, cache em disco |
| PSO Pre-warming | ✅ Integrado | Compilação antecipada de pipelines |
| Async Compute Dispatcher | ✅ Integrado | Dispatch assíncrono via command queue |
| Hi-Z Occlusion Culling | ✅ Integrado | Culling hierárquico de profundidade |
| VRS Tile Classification | ✅ Integrado | Classificação de tiles para shading variável |
| PE Loader + Import Shims | ⚠️ Parcial | Executável stand-in funcional; aguardando binário real |
| D3D12 Xbox ABI Proxy | ⚠️ Em progresso | Device, queues, command lists, descriptors |
| GDK Runtime (XTaskQueue, XAudio2) | ⚠️ Parcial | XTaskQueue vendored; XAudio2 pendente |
| GDeflate Decompressor | ❌ Pendente | Compressão de assets do Xbox |
| Boot / Story Mode / Jogável | ❌ Não | Bloqueado pelo executável do jogo |

---

## Como Compilar

### Pré-requisitos

- **Windows 10/11 x64** com ~350 GB livres para arquivos de trabalho
- **Sua cópia do Xbox Series** (versão 1.0.2.1), com arquivos legíveis em disco
- **Python 3.12+** (marcar "Add python.exe to PATH")
- **.NET 8 SDK**
- **Vulkan SDK** (spirv-as / spirv-val)
- **LLVM 21+** com target AMDGPU (para llvm-mc)
- **Visual Studio 2022** com "Desktop development with C++" e Windows SDK 10.0.26100+
- **ffmpeg** no PATH (opcional: decodifica streams XMA2)

### Build rápido

```powershell
# Clonar com submódulo
git clone --recursive https://github.com/Hylanderry/DedSec.git
cd DedSec

# Build completo (detecta sua cópia automaticamente)
.\Build.bat

# Ou via terminal com controle total
python -I make_game.py --find                          # listar cópias encontradas
python -I make_game.py --check                         # verificar sem compilar
python -I make_game.py --game "D:\seu\jogo" --out "D:\Games\DedSec" --work "E:\dedsec-work"
python -I make_game.py --stages shaders,assets         # apenas etapas específicas
```

O resultado vai para `Documents\DedSec`. Um build interrompido continua de onde parou.

### Variáveis de ambiente

| Variável | Descrição |
|----------|-----------|
| `DEDSEC_GAME_DIR` | Caminho para a cópia do jogo |
| `DEDSEC_MOVIE` | Caminho para cutscenes Bink |
| `DEDSEC_D3D12` | Backend: `vulkan` ou `native` (padrão: native) |
| `DEDSEC_W64_EMULATE` | Emulação wave64: `auto`, `on`, `off` |
| `DEDSEC_VK_VALIDATION` | Ativar validação Vulkan (`1`) |
| `DEDSEC_VULKAN_LOADER` | Caminho customizado para libvulkan |

---

## Como Funciona

```
┌─────────────────────────────────────────────────────────┐
│                  CÓPIA DO JOGO (Xbox)                   │
│  shaders.rpf ─► DXBC/FSSM ─► RDNA2 ISA (gfx1013)       │
│  models.rpf  ─► RPF9 ─► Malhas + Texturas + Áudio      │
└──────────────────────┬──────────────────────────────────┘
                       │
          ┌────────────▼────────────┐
          │   TRADUTOR DEDSEC       │
          │  RDNA2 → HLSL SM 6.6   │
          │  (VOP3/SDWA/MIMG/DPP)  │
          └────────────┬────────────┘
                       │
          ┌────────────▼────────────┐
          │   COMPILADOR DXC        │
          │  HLSL → DXIL SM 6.6    │
          │  (dxcompiler.dll)       │
          └────────────┬────────────┘
                       │
          ┌────────────▼────────────┐
          │   RUNTIME DEDSEC        │
          │  PE Loader + Shims      │
          │  D3D12 Native Pipeline  │
          │  Input Adapter (XInput) │
          │  FSR3 + VRS + Cache     │
          └────────────┬────────────┘
                       │
              ┌────────▼────────┐
              │   GPU WINDOWS   │
              │   DirectX 12    │
              └─────────────────┘
```

| Componente | Função | Localização |
|------------|--------|-------------|
| Extração de shaders | Carve DXBC de shaders.rpf, unpack FSSM→RDNA2 | `tools/shader_carve.py`, `fssm_unpack.py` |
| Tradutor ISA | RDNA2 mnemonics → HLSL SM 6.6 | `runtime/shims/rdna2_to_hlsl.cpp`, `rdna2_mimg.cpp` |
| Compilador DXC | HLSL → DXIL via dxcompiler.dll | `runtime/shims/dedsec_dxc_compiler.cpp` |
| Adaptador de input | Xbox GameInput → XInput → Teclado | `runtime/shims/xbox_input_adapter.cpp` |
| Runtime PE | Mapeia PE, resolve imports para shims | `runtime/src/` |
| Otimizações DX12 | FSR3, VRS, cache, PSO warmup, async compute | `runtime/shims/dedsec_*.cpp` |

A documentação técnica completa está em [`docs/design.md`](docs/design.md) e o índice de notas de formato em [`docs/README.md`](docs/README.md).

---

## Quer Contribuir?

As maiores peças em aberto:

1. **Tradutor de shaders completo**: FLAT, MUBUF, SOP, VINTRP, EXP, MTBUF
2. **GDeflate decompressor**: compressão de assets do Xbox
3. **XAudio2 shim**: áudio do jogo
4. **Boot do executável real**: PE loader testado contra o binário do jogo
5. **Model layouts**: formatos de malha que o decoder ainda rejeita

Veja [`CONTRIBUTING.md`](CONTRIBUTING.md). **Nunca anexe arquivos do jogo, assets extraídos ou shaders gerados em issues ou pull requests.**

---

## Créditos

- **Comunidade de engenharia reversa**: pesquisa coletiva sobre formatos RPF9, FSSM, RDNA2 ISA e D3D12 Xbox ABI
- **Mesa** (MIT): layout de descritores GFX10, regras ACO/RADV como referência
- **libHttpClient** (MIT, Microsoft): implementação XTaskQueue/XAsync em `third_party/xtaskqueue`
- **LLVM**: desmontador AMDGPU (gfx1013)
- **SPIRV-Tools**: montagem e validação SPIR-V
- **rpf9-explorer** (breadbbbb): modelo RPF9, decoders de texture pool e AWC

*Grand Theft Auto e todos os nomes relacionados são marcas registradas de seus respectivos proprietários. Este projeto não é afiliado nem endossado por eles.*

---

## Licença

**MIT License** — veja [`LICENSE`](LICENSE) para detalhes.

```
Copyright (c) 2026 DedSec Team

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.
```