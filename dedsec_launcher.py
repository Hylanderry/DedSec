#!/usr/bin/env python3
"""
DedSec Launcher - Interface gráfica pra configurar e executar o runtime do GTA VI port.
"""

import tkinter as tk
from tkinter import ttk, scrolledtext, messagebox
import subprocess
import os
import json
import threading
import time
from pathlib import Path

class DedSecLauncher:
    def __init__(self, root):
        self.root = root
        self.root.title("DedSec Launcher - GTA VI Port")
        self.root.geometry("800x600")
        self.root.configure(bg='#1e1e1e')

        # Paths
        self.dedsec_dir = Path(__file__).parent
        self.runtime_exe = self.dedsec_dir / "runtime" / "build" / "dedsec_rt.exe"
        self.game_dir = Path("D:/Port GTA VI/Brute Data/GTA6.Xbox.v1.0.2.1-CYBERLEEK")
        self.settings_file = self.game_dir / "settings.ini"

        # Style
        self.style = ttk.Style()
        self.style.theme_use('clam')
        self.style.configure('TFrame', background='#1e1e1e')
        self.style.configure('TLabel', background='#1e1e1e', foreground='#e0e0e0', font=('Segoe UI', 10))
        self.style.configure('TButton', background='#3a6ea5', foreground='#ffffff', font=('Segoe UI', 10, 'bold'))
        self.style.map('TButton', background=[('active', '#4a7eb5')])
        self.style.configure('TLabelframe', background='#1e1e1e', foreground='#e0e0e0')
        self.style.configure('TLabelframe.Label', background='#1e1e1e', foreground='#4a9eff', font=('Segoe UI', 11, 'bold'))

        # Main container
        main_frame = ttk.Frame(self.root, padding="20")
        main_frame.pack(fill=tk.BOTH, expand=True)

        # Title
        title_label = tk.Label(main_frame, text="DEDSEC", font=('Segoe UI', 24, 'bold'),
                               bg='#1e1e1e', fg='#4a9eff')
        title_label.pack(pady=(0, 5))

        subtitle = tk.Label(main_frame, text="GTA VI Xbox → PC Port", font=('Segoe UI', 12),
                           bg='#1e1e1e', fg='#a0a0a0')
        subtitle.pack(pady=(0, 20))

        # Graphics Settings Frame
        graphics_frame = ttk.LabelFrame(main_frame, text="Configurações Gráficas", padding="15")
        graphics_frame.pack(fill=tk.X, pady=(0, 15))

        # Resolution
        res_frame = ttk.Frame(graphics_frame)
        res_frame.pack(fill=tk.X, pady=5)
        ttk.Label(res_frame, text="Resolução:").pack(side=tk.LEFT, padx=(0, 10))
        self.resolution_var = tk.StringVar(value="1280x720")
        res_combo = ttk.Combobox(res_frame, textvariable=self.resolution_var,
                                 values=["1280x720", "1600x900", "1920x1080", "2560x1440", "3840x2160"],
                                 state="readonly", width=15)
        res_combo.pack(side=tk.LEFT)

        # FPS Limit
        fps_frame = ttk.Frame(graphics_frame)
        fps_frame.pack(fill=tk.X, pady=5)
        ttk.Label(fps_frame, text="Limite de FPS:").pack(side=tk.LEFT, padx=(0, 10))
        self.fps_var = tk.StringVar(value="0")
        fps_combo = ttk.Combobox(fps_frame, textvariable=self.fps_var,
                                values=["0 (Desbloqueado)", "30", "60", "120", "144", "240"],
                                state="readonly", width=15)
        fps_combo.pack(side=tk.LEFT)
        fps_combo.bind('<<ComboboxSelected>>', self.on_fps_change)

        # Quality Settings
        quality_frame = ttk.Frame(graphics_frame)
        quality_frame.pack(fill=tk.X, pady=5)
        ttk.Label(quality_frame, text="Qualidade:").pack(side=tk.LEFT, padx=(0, 10))
        self.quality_var = tk.StringVar(value="Medium")
        quality_combo = ttk.Combobox(quality_frame, textvariable=self.quality_var,
                                    values=["Low", "Medium", "High", "Ultra"],
                                    state="readonly", width=15)
        quality_combo.pack(side=tk.LEFT)

        # Upscaler
        upscaler_frame = ttk.Frame(graphics_frame)
        upscaler_frame.pack(fill=tk.X, pady=5)
        ttk.Label(upscaler_frame, text="Upscaler:").pack(side=tk.LEFT, padx=(0, 10))
        self.upscaler_var = tk.StringVar(value="Native")
        upscaler_combo = ttk.Combobox(upscaler_frame, textvariable=self.upscaler_var,
                                     values=["Native", "FSR2", "FSR3"],
                                     state="readonly", width=15)
        upscaler_combo.pack(side=tk.LEFT)

        # Action Buttons
        button_frame = ttk.Frame(main_frame)
        button_frame.pack(fill=tk.X, pady=(0, 15))

        self.play_btn = ttk.Button(button_frame, text="▶ JOGAR", command=self.launch_game)
        self.play_btn.pack(side=tk.LEFT, padx=(0, 10), ipady=10, ipadx=20)

        self.settings_btn = ttk.Button(button_frame, text="⚙ Salvar Config", command=self.save_settings)
        self.settings_btn.pack(side=tk.LEFT, ipady=10, ipadx=20)

        # Log Output
        log_frame = ttk.LabelFrame(main_frame, text="Log de Execução", padding="10")
        log_frame.pack(fill=tk.BOTH, expand=True)

        self.log_text = scrolledtext.ScrolledText(log_frame, wrap=tk.WORD, bg='#0a0a0a', fg='#00ff00',
                                                  font=('Consolas', 9), height=15)
        self.log_text.pack(fill=tk.BOTH, expand=True)
        self.log_text.insert(tk.END, "DedSec Launcher inicializado\n")
        self.log_text.insert(tk.END, f"Runtime: {self.runtime_exe}\n")
        self.log_text.insert(tk.END, f"Game: {self.game_dir}\n")
        self.log_text.config(state=tk.DISABLED)

        # Load existing settings
        self.load_settings()

        # Check runtime
        self.check_runtime()

    def on_fps_change(self, event=None):
        fps_text = self.fps_var.get()
        if "Desbloqueado" in fps_text:
            self.fps_var.set("0")
        else:
            self.fps_var.set(fps_text.split()[0])

    def load_settings(self):
        """Carrega configurações do settings.ini se existir"""
        if self.settings_file.exists():
            try:
                with open(self.settings_file, 'r') as f:
                    for line in f:
                        line = line.strip()
                        if '=' in line and not line.startswith(';'):
                            key, value = line.split('=', 1)
                            key = key.strip()
                            value = value.strip()

                            if key == 'RenderWidth':
                                # Procurar altura
                                pass
                            elif key == 'RenderHeight':
                                # Combinar com largura
                                pass
                            elif key == 'MaxFPS':
                                if value == '0':
                                    self.fps_var.set("0 (Desbloqueado)")
                                else:
                                    self.fps_var.set(value)
                            elif key == 'Upscaler':
                                self.upscaler_var.set(value)

                self.log("Configurações carregadas do settings.ini")
            except Exception as e:
                self.log(f"Erro ao carregar settings: {e}")

    def save_settings(self):
        """Salva configurações no settings.ini"""
        try:
            # Parse resolution
            res = self.resolution_var.get().split('x')
            width, height = int(res[0]), int(res[1])

            # Parse FPS
            fps_text = self.fps_var.get()
            fps = 0 if "Desbloqueado" in fps_text else int(fps_text.split()[0])

            # Quality mapping
            quality_map = {"Low": 0, "Medium": 1, "High": 2, "Ultra": 3}
            quality = quality_map.get(self.quality_var.get(), 1)

            # Write settings
            with open(self.settings_file, 'w') as f:
                f.write("; GTA VI PC Port - Configuration File\n")
                f.write("[Graphics]\n")
                f.write(f"RenderWidth={width}\n")
                f.write(f"RenderHeight={height}\n")
                f.write(f"MaxFPS={fps}\n")
                f.write(f"Upscaler={self.upscaler_var.get()}\n")
                f.write(f"TextureQuality={quality}\n")
                f.write(f"ShadowQuality={quality}\n")
                f.write(f"LODScale={1.0 + (quality * 0.25)}\n")

            self.log(f"Configurações salvas em {self.settings_file}")
            messagebox.showinfo("Sucesso", "Configurações salvas com sucesso!")
        except Exception as e:
            self.log(f"Erro ao salvar settings: {e}")
            messagebox.showerror("Erro", f"Erro ao salvar: {e}")

    def check_runtime(self):
        """Verifica se o runtime foi compilado"""
        if self.runtime_exe.exists():
            self.log(f"✓ Runtime encontrado: {self.runtime_exe.name}")
        else:
            self.log(f"✗ Runtime não encontrado: {self.runtime_exe}")
            self.log("Execute: python make_game.py --stages runtime")

    def launch_game(self):
        """Executa o runtime do DedSec"""
        if not self.runtime_exe.exists():
            messagebox.showerror("Erro", "Runtime não encontrado. Compile primeiro.")
            return

        # Salvar configurações antes de executar
        self.save_settings()

        self.log("\n" + "="*50)
        self.log("Iniciando DedSec Runtime...")
        self.log("="*50)

        # Desabilitar botão durante execução
        self.play_btn.config(state=tk.DISABLED)

        # Executar em thread separada
        thread = threading.Thread(target=self.run_runtime)
        thread.daemon = True
        thread.start()

    def run_runtime(self):
        """Executa o runtime e captura output"""
        try:
            env = os.environ.copy()
            env['DEDSEC_DEVICE_TYPE'] = '7'  # Anaconda (Series X)

            process = subprocess.Popen(
                [str(self.runtime_exe)],
                cwd=str(self.game_dir),
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
                env=env,
                bufsize=1,
                universal_newlines=True
            )

            # Ler output em tempo real
            for line in process.stdout:
                self.log(line.rstrip())
                self.root.update()

            process.wait()

            if process.returncode == 0:
                self.log("\n✓ Runtime finalizado com sucesso")
            else:
                self.log(f"\n✗ Runtime finalizou com código {process.returncode}")

        except Exception as e:
            self.log(f"\n✗ Erro ao executar runtime: {e}")

        finally:
            # Reabilitar botão
            self.root.after(0, lambda: self.play_btn.config(state=tk.NORMAL))

    def log(self, message):
        """Adiciona mensagem ao log"""
        self.log_text.config(state=tk.NORMAL)
        self.log_text.insert(tk.END, message + "\n")
        self.log_text.see(tk.END)
        self.log_text.config(state=tk.DISABLED)

def main():
    root = tk.Tk()
    app = DedSecLauncher(root)
    root.mainloop()

if __name__ == "__main__":
    main()