<p align="center">
 <img src="https://upload.wikimedia.org/wikipedia/commons/thumb/0/05/Flag_of_Brazil.svg/1200px-Flag_of_Brazil.svg.png" width="300" alt="Bandeira do Brasil">
</p>

<h1 align="center">🇧🇷 DedSec — GTA VI Xbox Series → PC</h1>

<p align="center">
 <strong>Projeto 100% brasileiro, feito por uma única pessoa.</strong><br>
 Tradução nativa RDNA2 → HLSL SM 6.6 → DirectX 12 para rodar GTA VI (Xbox Series) no Windows.<br>
 Sem Vulkan. Sem camadas extras. Performance máxima.
</p>

<p align="center">
 <a href="#status-do-projeto"><img src="https://img.shields.io/badge/status-em%20desenvolvimento-yellow?style=for-the-badge" alt="Status"></a>
 <a href="#licença"><img src="https://img.shields.io/badge/licença-GPLv3-blue?style=for-the-badge" alt="Licença"></a>
 <a href="https://github.com/Hylanderry/DedSec-Consumer-Boycott-PC-Port"><img src="https://img.shields.io/github/stars/Hylanderry/DedSec-Consumer-Boycott-PC-Port?style=for-the-badge&color=green" alt="Stars"></a>
</p>

---

## 🔥 Manifesto: A Pirataria É Culpa Deles

> **A comunidade está aqui para boicotar.** Nós somos os consumidores. A pirataria não nasce do nada — ela é o resultado direto de um serviço porco, de preços abusivos, de lançamentos quebrados e de empresas que tratam quem paga como otário.

Rockstar, Take-Two e Sony têm um histórico vergonhoso:

- **Lançamentos desastrosos** — jogos vendidos a preço cheio em estado injogável, enquanto os consumidores pagam o pato e servem de beta testers não remunerados.
- **Preços regionais absurdos** — cobrar o mesmo preço em dólar de países onde o salário mínimo não compra nem metade do jogo. Isso não é mercado, é exploração.
- **PC tratado como afterthought** — ports atrasados anos, otimização inexistente, features que existem no console mas "misteriosamente" não chegam ao PC. O consumidor de PC paga mais e recebe menos.
- **DRM agressivo contra quem comprou** — sistemas anti-pirataria que punem o jogador legítimo com perda de performance, crashes e requisitos artificiais, enquanto as versões crackeadas rodam melhor. Ironia? Não. Incompetência proposital.
- **Comunidade ignorada** — feedback tratado como ruído, bugs reportados há meses sem resposta, promessas vazias em roadmaps que nunca se materializam.

### A Steam Entendeu. Eles Não.

A **Steam** provou que é possível vender jogos no PC respeitando o consumidor: preços regionais justos, suporte a mods, achievements integradas, cloud saves, overlay, reembolso sem burocracia, e uma plataforma que evolui constantemente ouvindo a comunidade. A Steam não precisa de DRM draconiano porque entende que **um bom serviço é a melhor proteção contra pirataria**.

Quando você trata o consumidor com respeito, ele paga. Quando você trata como inimigo, ele encontra alternativas. **A pirataria é o mercado respondendo a um serviço ruim.** E nós, a comunidade, estamos aqui para mostrar que existe outro caminho — e que o boicote funciona.

Este projeto existe porque eles falharam. Porque o consumidor merece mais. Porque a comunidade brasileira não vai ficar esperando de braços cruzados enquanto cobram R$350 por um produto inacabado.

---

## ⚠️ Aviso Importante

> **Este repositório NÃO contém o jogo, chaves ou executáveis.** Você precisa da sua própria cópia do GTA VI para Xbox Series (versão 1.0.2.1), com os arquivos já acessíveis em disco. Nada aqui descriptografa ou contorna proteções. Tudo que o build gera (shaders traduzidos, assets extraídos, saves) vem do seu jogo e fica no seu PC. Nunca compartilhe esses arquivos.

---

## 🎯 O Que É o DedSec?

O DedSec é um projeto de **recompilação estática** focado em trazer GTA VI do Xbox Series S/X para o PC Windows usando uma abordagem **nativa DirectX 12**. Diferente de outros projetos que usam Vulkan como camada intermediária, o DedSec traduz os shaders RDNA2 do console diretamente para HLSL Shader Model 6.6, compilando via DXC (dxcompiler.dll) para DXIL nativo.

**Em português simples:** o jogo do Xbox usa shaders escritos para a GPU AMD RDNA2 do console. Em vez de converter esses shaders para SPIR-V e depois rodar via Vulkan (3 saltos, mais overhead), o DedSec converte direto para HLSL e roda nativamente no DirectX 12 do Windows (2 saltos, menos overhead).

### Por Que Isso Importa?

| Abordagem | Pipeline de Shaders | Saltos | Overhead |
|---|---|---|---|
| Leonida (mainline) | RDNA2 → SPIR-V → Vulkan → GPU | 3 | Alto |
| AnyXboxSeries | RDNA2 → HLSL SM 6.6 → DXIL → DX12 | 2 | Médio |
| **DedSec** 🇧🇷 | RDNA2 → HLSL SM 6.6 → DXC → DXIL → DX12 Nativo | 2 | **Mínimo** |

O DedSec não é um fork copiado. É uma **implementação independente** que entende a arquitetura e faz melhor onde importa: pipeline gráfico nativo, compatibilidade Zen 2, e otimizações D3DX12 que outros projetos ignoraram.

---

## 📊 Status do Projeto

| Componente | Status | Detalhes |
|---|---|---|
| Verificação da cópia (Xbox Series 1.0.2.1) | ✅ Funcional | Detecta versão por tamanho de arquivo |
| Shaders: extração e desmontagem RDNA2 | ✅ 26.503/26.503 | Todas as categorias ISA gfx1030 cobertas |
| Tradutor RDNA2 → HLSL SM 6.6 | 🔧 Em progresso | Header completo (1.933 formas ISA, 19 categorias), implementação em andamento |
| Proxy DX12 Nativo | ✅ Criado | vtable proxy roteando Xbox ABI → ID3D12Device nativo com cache PSO |
| Compatibilidade Zen 2 (VEH) | ✅ Implementado | CLZERO, MONITORX, MWAITX, SSE4a via Vectored Exception Handler |
| GDeflate Codec | ❌ Não iniciado | Próximo marco após tradutor de shaders |
| Boot / Modo História / Jogável | ❌ Não | Trabalho futuro |

---

## 🏗️ Arquitetura

### Componentes Principais

#### 1. `d3d12_native_device.cpp` — Proxy DX12 Nativo
Adapta o padrão de proxy vtable para rotear chamadas da ABI do Xbox D3D12 diretamente para um device DX12 nativo no PC. Inclui cache de Pipeline State Objects (PSO) e tradução de shaders em tempo real. Elimina a necessidade de Vulkan.

#### 2. `rdna2_to_hlsl.h` — Tradutor RDNA2 → HLSL SM 6.6
Cobre todas as **1.933 formas de instrução ISA gfx1030** organizadas em 19 categorias:
- **VOP3** (425 formas) — ALU vetorial de 3 operandos
- **SDWA** (220) — endereçamento sub-dword
- **FLAT** (133) — memória flat/global
- **MIMG** (111) — instruções de imagem/textura
- **DPP8/16** (200) — permutação data-paralela
- **SMEM** (84) — memória escalar
- **MUBUF** (73) — memória buffer não-tipada
- **SOP1/2/K/C/P** (197) — operações escalares
- **VOP1/2/3P** (157) — operações vetoriais empacotadas
- **EXP** (1) — exportação de pixel shader
- E mais: VINTRP, MTBUF, SOPP

#### 3. `zen2_compat.h` — Emulação Zen 2 via VEH
Vectored Exception Handler que intercepta exceções `#UD` para instruções Zen 2 que podem não existir em CPUs x64 genéricas:
- **CLZERO** (`0F 01 FC`) — zeroing de cache line (64 bytes)
- **MONITORX** (`0F 01 FA`) — extended monitor (stub)
- **MWAITX** (`0F 01 FB`) — extended wait com aproximação via Sleep
- **SSE4a** — EXTRQ, INSERTQ, MOVNTSD, MOVNTSS

---

## 🔧 Requisitos

- Windows 10/11 x64
- Visual Studio 2022 com C++20 e Windows SDK 10.0.26100+
- GPU compatível com DirectX 12 SM 6.6 (NVIDIA Turing+, AMD RDNA+)
- Python 3.12+ (com PATH configurado)
- .NET 8 SDK
- `dxcompiler.dll` (Windows SDK ou dump Cyberleek)
- ~350 GB livres para arquivos de trabalho

---

## 🚀 Como Compilar

```bash
cd runtime/build
cmake .. -G Ninja -DCMAKE_BUILD_TYPE=Release
ninja
```

Ou pelo launcher Python:
```bash
python leonida_launcher.py
```

---

## 🤝 Quer Contribuir?

As maiores peças em aberto:

1. **Implementação completa do tradutor RDNA2 → HLSL** — o header cobre todas as 1.933 formas; falta a lógica de tradução por categoria
2. **Codec GDeflate** — compressão de texturas usada pelo Xbox Series
3. **Layouts de modelos** que o decoder ainda rejeita
4. **Testes em hardware real** — validação dos shaders traduzidos em GPUs NVIDIA e AMD

Veja `CONTRIBUTING.md` para diretrizes. **Nunca anexe arquivos do jogo, assets extraídos ou shaders gerados em issues ou pull requests.**

---

## 🙏 Créditos e Reconhecimentos

- **Leonida** (ItsNotRexed): base arquitetural e ferramentas de extração de shaders/assets
- **AnyXboxSeries**: referência na abordagem DX12 nativa e benchmarks de cobertura ISA
- **rpf9-explorer** (breadbbbb): decoders RPF9, texture-pool e AWC
- **Mesa** (MIT): layout de resource descriptors GFX10
- **LLVM**: desmontador AMDGPU
- **SPIRV-Tools**: montagem e validação
- **Microsoft libHttpClient** (MIT): implementação XTaskQueue/XAsync
- **Steam**: prova viva de que tratar o consumidor com respeito é o melhor modelo de negócios

> Grand Theft Auto e todos os nomes relacionados são marcas registradas de seus respectivos proprietários. Este projeto não é afiliado nem endossado por eles.

---

## 📜 Licença

GPLv3 (ou qualquer versão posterior). Veja `LICENSE`. Código de terceiros mantém suas próprias licenças.

---

## 🏷️ Assinatura DedSec

Todos os arquivos novos deste projeto contêm o header:
```cpp
// DedSec Fork — DX12 Native Pipeline
```

---

<p align="center">
 <em>"A pirataria é o grito do consumidor que foi ignorado. Nós somos esse grito."</em><br>
 <strong>— DedSec 🇧🇷</strong>
</p>

<p align="center">
 Feito com 💚💛💙 no Brasil<br>
 <strong>A comunidade não perdoa serviço porco.</strong>
</p>