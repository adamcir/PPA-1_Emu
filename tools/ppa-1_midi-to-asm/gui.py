"""Tkinter editor for per-track MIDI-to-ASM settings (optional standard library)."""
from pathlib import Path
from midi_to_asm import (WAVES, POLY, MidiError, Settings, parse_midi,
                         defaults, validate, save_config, load_config, render)

def launch(initial_file=None, clock_hz=100000, tick_ms=10):
    try:
        import tkinter as tk
        from tkinter import ttk, filedialog, messagebox
    except ImportError as exc:
        raise MidiError("Tkinter unavailable; install python3-tk or use CLI") from exc

    root = tk.Tk()
    root.title("PPA-1_MIDI-to-ASM  |  MIDI track mixer")
    root.geometry("1080x670")
    file_var = tk.StringVar(value=str(initial_file or ""))
    clock_var = tk.StringVar(value=str(clock_hz))
    tick_var = tk.StringVar(value=str(tick_ms))
    current = [None]
    row_vars = {}
    outer = ttk.Frame(root, padding=14)
    outer.pack(fill="both", expand=True)

    top = ttk.Frame(outer)
    top.pack(fill="x")
    ttk.Label(top, text="MIDI").pack(side="left")
    ttk.Entry(top, textvariable=file_var).pack(side="left", fill="x",
                                               expand=True, padx=(8, 8))
    body = ttk.Frame(outer)
    body.pack(fill="both", expand=True, pady=14)
    scrollbar = ttk.Scrollbar(body, orient="vertical")
    scrollbar.pack(side="right", fill="y")
    canvas = tk.Canvas(body, borderwidth=0, highlightthickness=0,
                       yscrollcommand=scrollbar.set)
    canvas.pack(side="left", fill="both", expand=True)
    scrollbar.configure(command=canvas.yview)
    table = ttk.Frame(canvas)
    canvas.create_window((0, 0), window=table, anchor="nw")
    table.bind("<Configure>", lambda e: canvas.configure(
        scrollregion=canvas.bbox("all")))

    def show_error(fn):
        try:
            fn()
        except (OSError, ValueError, MidiError, TypeError) as exc:
            messagebox.showerror("PPA-1_MIDI-to-ASM", str(exc))

    def read_table():
        if current[0] is None:
            raise MidiError("Load MIDI first")
        config = {}
        for track in current[0][0]:
            vals = [v.get() for v in row_vars[track.id]]
            config[track.id] = Settings(
                voice=int(vals[0]), waveform=vals[1], polyphony=vals[2],
                volume=int(vals[3]), transpose=int(vals[4]))
        validate(current[0][0], config)
        return config

    def fill_rows(config):
        for child in table.winfo_children():
            child.destroy()
        row_vars.clear()
        labels = ("MIDI track / channel", "Notes", "Voice (0=off)",
                  "Waveform", "Polyphony", "Volume", "Transpose")
        for j, label in enumerate(labels):
            ttk.Label(table, text=label, padding=6).grid(
                row=0, column=j, sticky="w")
        for i, track in enumerate(current[0][0], start=1):
            cfg = config[track.id]
            vars_ = (tk.StringVar(value=str(cfg.voice)),
                     tk.StringVar(value=cfg.waveform),
                     tk.StringVar(value=cfg.polyphony),
                     tk.StringVar(value=str(cfg.volume)),
                     tk.StringVar(value=str(cfg.transpose)))
            row_vars[track.id] = vars_
            ttk.Label(table, text=track.name, width=34).grid(
                row=i, column=0, sticky="w")
            ttk.Label(table, text=str(track.notes)).grid(row=i, column=1)
            for column, choices, width in (
                (2, ("0", "1", "2", "3", "4"), 10),
                (3, WAVES, 10),
                (4, POLY, 10)):
                ttk.Combobox(table, textvariable=vars_[column - 2],
                             values=choices, state="readonly", width=width).grid(
                                 row=i, column=column, padx=2)
            ttk.Entry(table, textvariable=vars_[3], width=10).grid(
                row=i, column=5, padx=2)
            ttk.Entry(table, textvariable=vars_[4], width=10).grid(
                row=i, column=6, padx=2)

    def open_midi():
        path = Path(file_var.get())
        current[0] = parse_midi(path)
        fill_rows(defaults(current[0][0]))

    def browse():
        path = filedialog.askopenfilename(
            filetypes=[("MIDI files", "*.mid *.midi"), ("All files", "*.*")])
        if path:
            file_var.set(path)
            show_error(open_midi)

    ttk.Button(top, text="Browse...", command=browse).pack(side="left")
    ttk.Button(top, text="Load", command=lambda: show_error(open_midi)).pack(
        side="left", padx=6)
    bottom = ttk.Frame(outer)
    bottom.pack(fill="x", pady=4)
    ttk.Label(bottom, text="Clock, microsteps/s").pack(side="left")
    ttk.Entry(bottom, textvariable=clock_var, width=12).pack(
        side="left", padx=6)
    ttk.Label(bottom, text="Time resolution, ms").pack(
        side="left", padx=(14, 4))
    ttk.Entry(bottom, textvariable=tick_var, width=8).pack(side="left")

    def export():
        config = read_table()
        code = render(current[0], config, Path(file_var.get()).name,
                      int(tick_var.get()), int(clock_var.get()))
        path = filedialog.asksaveasfilename(defaultextension=".asm",
                    filetypes=[("PPA-1 ASM", "*.asm")])
        if path:
            Path(path).write_text(code, encoding="utf-8")
            messagebox.showinfo("ASM exported",
                 "Saved: " + path + "\nCopy into examples/ to resolve includes.")

    def store():
        config = read_table()
        path = filedialog.asksaveasfilename(defaultextension=".json",
                    filetypes=[("Track config", "*.json")])
        if path:
            save_config(path, current[0][0], config)

    def restore():
        if current[0] is None:
            raise MidiError("Load MIDI first")
        path = filedialog.askopenfilename(filetypes=[("Track config", "*.json")])
        if path:
            fill_rows(load_config(path, current[0][0]))

    actions = ttk.Frame(outer)
    actions.pack(fill="x", pady=8)
    ttk.Button(actions, text="Load settings", command=lambda:
               show_error(restore)).pack(side="left")
    ttk.Button(actions, text="Save settings", command=lambda:
               show_error(store)).pack(side="left", padx=8)
    ttk.Button(actions, text="Export ASM", command=lambda:
               show_error(export)).pack(side="right")
    ttk.Label(outer, text="One track/channel per oscillator. Polyphony: "
              "low / high / latest. MIDI velocity, sustain and CC7/CC11 supported.").pack(
                  anchor="w", pady=4)
    if initial_file:
        show_error(open_midi)
    root.mainloop()
