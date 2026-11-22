#!/usr/bin/env python3
"""
DedSec Rebranding Script
Substitui todas as referências ao DedSec e faz o rebranding completo do projeto.

Uso: python scripts/rebrand_to_dedsec.py
"""

import os
import re
from pathlib import Path

REPO_ROOT = Path(__file__).parent.parent

# Mapeamento de substituições
SUBSTITUTIONS = {
    # Nomes do projeto
    'DedSec': 'DedSec',
    'dedsec': 'dedsec',
    'DEDSEC': 'DEDSEC',

    # URLs do GitHub
    'github.com/ItsNotRexed/DedSec': 'github.com/Hylanderry/DedSec-Consumer-Boycott-PC-Port',
    'ItsNotRexed/DedSec': 'Hylanderry/DedSec-Consumer-Boycott-PC-Port',

    # Variáveis de ambiente
    'DEDSEC_GAME_DIR': 'DEDSEC_GAME_DIR',
    'DEDSEC_MOVIE': 'DEDSEC_MOVIE',
    'DEDSEC_D3D12': 'DEDSEC_D3D12',
    'DEDSEC_VULKAN_LOADER': 'DEDSEC_VULKAN_LOADER',
    'DEDSEC_VULKAN': 'DEDSEC_VULKAN',
    'DEDSEC_W64_EMULATE': 'DEDSEC_W64_EMULATE',
    'DEDSEC_VK_VALIDATION': 'DEDSEC_VK_VALIDATION',
    'DEDSEC_D3D12=vulkan': 'DEDSEC_D3D12=vulkan',

    # Paths e diretórios
    'Documents\\DedSec': 'Documents\\DedSec',
    'Documents/DedSec': 'Documents/DedSec',
    'D:\\Port GTA VI\\AI\\dedsec': 'D:\\Port GTA VI\\AI\\DedSec',

    # Referências em código
    'dedsec_rt': 'dedsec_rt',  # Nome do executável principal
}

# Arquivos a renomear
FILE_RENAMES = {
    'dedsec_launcher.py': 'dedsec_launcher.py',
    'README_DEDSEC.md': 'README.md',  # Sobrescreve o README antigo
}

# Padrões de licença GPL a serem substituídos por MIT
GPL_PATTERNS = [
    (r'GPL-3\.0-or-later', 'MIT'),
    (r'GPL-3\.0-only', 'MIT'),
    (r'GPL-2\.0-only', 'MIT'),
    (r'SPDX-License-Identifier: GPL-3\.0-or-later', 'SPDX-License-Identifier: MIT'),
    (r'SPDX-License-Identifier: GPL-3\.0-only', 'SPDX-License-Identifier: MIT'),
]

def should_process_file(filepath: Path) -> bool:
    """Verifica se o arquivo deve ser processado."""
    # Ignora diretórios .git e build
    parts = filepath.parts
    if '.git' in parts or 'build' in parts or '__pycache__' in parts:
        return False

    # Processa apenas arquivos de texto
    text_extensions = {
        '.py', '.cpp', '.c', '.h', '.hpp', '.md', '.txt', '.bat', '.ps1',
        '.yml', '.yaml', '.json', '.cmake', '.txt', '.asm', '.cs'
    }

    if filepath.suffix.lower() in text_extensions:
        return True

    # Arquivos sem extensão que podem ser texto
    if filepath.name in {'CMakeLists.txt', 'Makefile', 'LICENSE'}:
        return True

    return False

def rebrand_content(content: str, filepath: Path) -> str:
    """Aplica todas as substituições ao conteúdo do arquivo."""
    original = content

    # Substituições de texto
    for old, new in SUBSTITUTIONS.items():
        content = content.replace(old, new)

    # Substituições de licença
    for pattern, replacement in GPL_PATTERNS:
        content = re.sub(pattern, replacement, content)

    # Casos especiais por tipo de arquivo
    if filepath.name == 'LICENSE' or 'license' in filepath.name.lower():
        # Substitui o texto completo da GPL por MIT
        if 'GNU GENERAL PUBLIC LICENSE' in content:
            content = """MIT License

Copyright (c) 2026 DedSec Team

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
"""

    return content

def main():
    print("=== DedSec Rebranding Script ===")
    print(f"Repositório: {REPO_ROOT}")
    print()

    # Coleta todos os arquivos a processar
    files_to_process = []
    for root, dirs, files in os.walk(REPO_ROOT):
        # Remove diretórios a ignorar
        dirs[:] = [d for d in dirs if d not in {'.git', 'build', '__pycache__', 'node_modules'}]

        for filename in files:
            filepath = Path(root) / filename
            if should_process_file(filepath):
                files_to_process.append(filepath)

    print(f"Arquivos encontrados: {len(files_to_process)}")

    # Processa cada arquivo
    modified_count = 0
    for filepath in files_to_process:
        try:
            with open(filepath, 'r', encoding='utf-8', newline='') as f:
                original_content = f.read()

            new_content = rebrand_content(original_content, filepath)

            if new_content != original_content:
                with open(filepath, 'w', encoding='utf-8', newline='') as f:
                    f.write(new_content)
                modified_count += 1
                print(f"  ✓ Modificado: {filepath.relative_to(REPO_ROOT)}")

        except (UnicodeDecodeError, PermissionError) as e:
            print(f"  ✗ Ignorado: {filepath.relative_to(REPO_ROOT)} ({e})")

    print()
    print(f"Arquivos modificados: {modified_count}/{len(files_to_process)}")

    # Renomeia arquivos
    print()
    print("Renomeando arquivos...")
    for old_name, new_name in FILE_RENAMES.items():
        old_path = REPO_ROOT / old_name
        new_path = REPO_ROOT / new_name

        if old_path.exists():
            if new_path.exists():
                print(f"  ⚠ {new_name} já existe, removendo...")
                new_path.unlink()

            old_path.rename(new_path)
            print(f"  ✓ {old_name} → {new_name}")

    print()
    print("=== Rebranding completo! ===")
    print()
    print("Próximos passos:")
    print("  1. Revise as mudanças: git diff")
    print("  2. Commit: git add -A && git commit -m 'refactor: Complete rebranding to DedSec'")
    print("  3. Push: git push dedsec main")

if __name__ == '__main__':
    main()