#!/usr/bin/env python3
from __future__ import annotations

import json
import os
import queue
import subprocess
import threading
from pathlib import Path
import tkinter as tk
from tkinter import filedialog, messagebox, ttk
from tkinter.scrolledtext import ScrolledText


ROOT = Path(__file__).resolve().parent
MODEL_ROOM = ROOT / "model_room"
DEFAULT_BINARY = ROOT / "gguf_context_fix"
DEFAULT_LIB_DIR = Path("/usr/local/lib/GlobalLibs")
PROMPT_TEXT = "chat> "


def discover_default_model() -> str:
    env_model = os.environ.get("GGUF_CHAT_MODEL", "").strip()
    if env_model and Path(env_model).exists():
        return env_model

    manifests = sorted(MODEL_ROOM.glob("*.manifest.json"), key=lambda p: p.stat().st_mtime, reverse=True)
    for manifest in manifests:
        try:
            data = json.loads(manifest.read_text(encoding="utf-8"))
        except Exception:
            continue
        model_path = str(data.get("model_path", "")).strip()
        if model_path and Path(model_path).exists():
            return model_path
    return ""


def discover_manifest() -> dict:
    manifests = sorted(MODEL_ROOM.glob("*.manifest.json"), key=lambda p: p.stat().st_mtime, reverse=True)
    for manifest in manifests:
        try:
            data = json.loads(manifest.read_text(encoding="utf-8"))
            data["_manifest_path"] = str(manifest)
            return data
        except Exception:
            continue
    return {}


class ShellSession:
    def __init__(self, event_queue: "queue.Queue[tuple[str, object]]") -> None:
        self.event_queue = event_queue
        self.process: subprocess.Popen[str] | None = None
        self.reader_thread: threading.Thread | None = None

    def start(self, binary_path: str, model_path: str, lib_dir: str) -> None:
        env = os.environ.copy()
        if lib_dir:
            env["DYLD_LIBRARY_PATH"] = lib_dir
        self.process = subprocess.Popen(
            [binary_path, model_path, "--shell"],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            encoding="utf-8",
            errors="replace",
            bufsize=1,
            env=env,
        )
        self.reader_thread = threading.Thread(target=self._reader_loop, daemon=True)
        self.reader_thread.start()

    def _reader_loop(self) -> None:
        if self.process is None or self.process.stdout is None:
            self.event_queue.put(("exit", -1))
            return
        while True:
            chunk = self.process.stdout.read(1)
            if chunk:
                self.event_queue.put(("chunk", chunk))
                continue
            if self.process.poll() is not None:
                break
        self.event_queue.put(("exit", self.process.returncode))

    def send(self, text: str) -> None:
        if self.process is None or self.process.stdin is None:
            raise RuntimeError("session_not_started")
        self.process.stdin.write(text + "\n")
        self.process.stdin.flush()

    def stop(self) -> None:
        if self.process is None:
            return
        if self.process.poll() is None:
            try:
                if self.process.stdin is not None:
                    self.process.stdin.write("/quit\n")
                    self.process.stdin.flush()
            except Exception:
                pass
            try:
                self.process.terminate()
                self.process.wait(timeout=2.0)
            except Exception:
                self.process.kill()
        self.process = None


class ChatWindow:
    def __init__(self, root: tk.Tk) -> None:
        self.root = root
        self.root.title("GGUF Context Fix Chat Window")
        self.root.geometry("1180x780")

        self.event_queue: "queue.Queue[tuple[str, object]]" = queue.Queue()
        self.session: ShellSession | None = None
        self.pending_role: str | None = None
        self.output_buffer = ""
        self.ready_for_input = False

        self.binary_var = tk.StringVar(value=str(DEFAULT_BINARY))
        self.model_var = tk.StringVar(value=discover_default_model())
        self.lib_var = tk.StringVar(value=str(DEFAULT_LIB_DIR))
        self.status_var = tk.StringVar(value="Idle")
        self.family_var = tk.StringVar(value="-")
        self.room_var = tk.StringVar(value="-")
        self.runtime_var = tk.StringVar(value="-")
        self.manifest_var = tk.StringVar(value="-")

        self._build_ui()
        self._load_resident_info()
        self.root.after(60, self._poll_events)
        self.root.protocol("WM_DELETE_WINDOW", self._on_close)

    def _build_ui(self) -> None:
        self.root.columnconfigure(0, weight=1)
        self.root.rowconfigure(2, weight=1)

        toolbar = ttk.Frame(self.root, padding=10)
        toolbar.grid(row=0, column=0, sticky="ew")
        toolbar.columnconfigure(1, weight=1)
        toolbar.columnconfigure(3, weight=1)
        toolbar.columnconfigure(5, weight=1)

        ttk.Label(toolbar, text="Binary").grid(row=0, column=0, sticky="w")
        ttk.Entry(toolbar, textvariable=self.binary_var).grid(row=0, column=1, sticky="ew", padx=(6, 12))
        ttk.Label(toolbar, text="Model").grid(row=0, column=2, sticky="w")
        ttk.Entry(toolbar, textvariable=self.model_var).grid(row=0, column=3, sticky="ew", padx=(6, 12))
        ttk.Button(toolbar, text="Browse", command=self._browse_model).grid(row=0, column=4, padx=(0, 12))
        ttk.Label(toolbar, text="Libs").grid(row=0, column=5, sticky="w")
        ttk.Entry(toolbar, textvariable=self.lib_var).grid(row=0, column=6, sticky="ew", padx=(6, 12))

        button_row = ttk.Frame(toolbar)
        button_row.grid(row=1, column=0, columnspan=7, sticky="ew", pady=(10, 0))
        ttk.Button(button_row, text="Start Resident Chat", command=self.start_session).pack(side="left")
        ttk.Button(button_row, text="Stop", command=self.stop_session).pack(side="left", padx=(8, 0))
        ttk.Button(button_row, text="Health", command=self.send_health).pack(side="left", padx=(8, 0))
        ttk.Button(button_row, text="Roommate", command=self.send_roommate).pack(side="left", padx=(8, 0))
        ttk.Button(button_row, text="Clear", command=self.clear_chat).pack(side="left", padx=(8, 0))
        ttk.Button(button_row, text="Refresh Resident Info", command=self._load_resident_info).pack(side="left", padx=(8, 0))
        ttk.Label(button_row, textvariable=self.status_var).pack(side="right")

        info = ttk.LabelFrame(self.root, text="Resident Info", padding=10)
        info.grid(row=1, column=0, sticky="ew", padx=10, pady=(0, 10))
        info.columnconfigure(1, weight=1)

        self._add_info_row(info, 0, "Family", self.family_var)
        self._add_info_row(info, 1, "Room", self.room_var)
        self._add_info_row(info, 2, "Runtime LMDB", self.runtime_var)
        self._add_info_row(info, 3, "Manifest", self.manifest_var)

        body = ttk.Panedwindow(self.root, orient=tk.VERTICAL)
        body.grid(row=2, column=0, sticky="nsew", padx=10, pady=(0, 10))

        transcript_frame = ttk.LabelFrame(body, text="Transcript", padding=8)
        compose_frame = ttk.LabelFrame(body, text="Compose", padding=8)
        body.add(transcript_frame, weight=4)
        body.add(compose_frame, weight=1)

        transcript_frame.rowconfigure(0, weight=1)
        transcript_frame.columnconfigure(0, weight=1)
        self.transcript = ScrolledText(transcript_frame, wrap="word", font=("Menlo", 12))
        self.transcript.grid(row=0, column=0, sticky="nsew")
        self.transcript.configure(state="disabled")

        compose_frame.rowconfigure(0, weight=1)
        compose_frame.columnconfigure(0, weight=1)
        self.input_text = ScrolledText(compose_frame, wrap="word", height=6, font=("Menlo", 12))
        self.input_text.grid(row=0, column=0, sticky="nsew")
        self.input_text.bind("<Return>", self._on_return)
        self.input_text.bind("<Shift-Return>", self._on_shift_return)
        ttk.Button(compose_frame, text="Send", command=self.send_prompt).grid(row=0, column=1, padx=(10, 0), sticky="ns")

    def _add_info_row(self, parent: ttk.LabelFrame, row: int, title: str, variable: tk.StringVar) -> None:
        ttk.Label(parent, text=title).grid(row=row, column=0, sticky="nw", pady=2)
        ttk.Label(parent, textvariable=variable, wraplength=920).grid(row=row, column=1, sticky="nw", padx=(8, 0), pady=2)

    def _browse_model(self) -> None:
        chosen = filedialog.askopenfilename(
            title="Select GGUF model",
            filetypes=[("GGUF files", "*.gguf"), ("All files", "*.*")],
        )
        if chosen:
            self.model_var.set(chosen)

    def _load_resident_info(self) -> None:
        data = discover_manifest()
        self.family_var.set(str(data.get("family", "-")))
        self.room_var.set(str(data.get("room", "-")))
        self.runtime_var.set(str(data.get("runtime_lmdb_path", "-")))
        self.manifest_var.set(str(data.get("_manifest_path", "-")))
        if not self.model_var.get().strip():
            model_path = str(data.get("model_path", "")).strip()
            if model_path:
                self.model_var.set(model_path)

    def append_transcript(self, speaker: str, text: str) -> None:
        if not text.strip():
            return
        self.transcript.configure(state="normal")
        self.transcript.insert("end", f"{speaker}:\n{text.strip()}\n\n")
        self.transcript.see("end")
        self.transcript.configure(state="disabled")

    def append_system(self, text: str) -> None:
        cleaned = text.strip()
        if cleaned:
            self.append_transcript("System", cleaned)

    def _clean_model_text(self, text: str) -> str:
        lines = []
        for raw_line in text.replace("\r", "").splitlines():
            stripped = raw_line.strip()
            if stripped and set(stripped) == {"."}:
                continue
            lines.append(raw_line)
        return "\n".join(lines).strip()

    def start_session(self) -> None:
        binary_path = self.binary_var.get().strip()
        model_path = self.model_var.get().strip()
        lib_dir = self.lib_var.get().strip()

        if not binary_path or not Path(binary_path).exists():
            messagebox.showerror("Missing Binary", "gguf_context_fix binary not found.")
            return
        if not model_path or not Path(model_path).exists():
            messagebox.showerror("Missing Model", "Select a valid GGUF model file.")
            return

        self.stop_session()
        self.output_buffer = ""
        self.ready_for_input = False
        self.pending_role = None
        self.session = ShellSession(self.event_queue)
        self.status_var.set("Starting resident shell...")
        try:
            self.session.start(binary_path, model_path, lib_dir)
        except Exception as exc:
            self.session = None
            self.status_var.set("Start failed")
            messagebox.showerror("Start Failed", str(exc))

    def stop_session(self) -> None:
        if self.session is not None:
            self.session.stop()
            self.session = None
        self.ready_for_input = False
        self.pending_role = None
        self.status_var.set("Stopped")

    def clear_chat(self) -> None:
        self.transcript.configure(state="normal")
        self.transcript.delete("1.0", "end")
        self.transcript.configure(state="disabled")

    def send_prompt(self) -> None:
        text = self.input_text.get("1.0", "end").strip()
        if not text:
            return
        if self.session is None or not self.ready_for_input:
            messagebox.showwarning("Shell Not Ready", "Start the resident chat first.")
            return
        self.append_transcript("You", text)
        self.input_text.delete("1.0", "end")
        self.pending_role = "assistant"
        self.ready_for_input = False
        self.status_var.set("Generating...")
        try:
            self.session.send(text)
        except Exception as exc:
            self.append_system(f"send_failed: {exc}")
            self.status_var.set("Send failed")

    def send_health(self) -> None:
        self._send_command("/health")

    def send_roommate(self) -> None:
        self._send_command("/roommate")

    def _send_command(self, command_text: str) -> None:
        if self.session is None or not self.ready_for_input:
            messagebox.showwarning("Shell Not Ready", "Start the resident chat first.")
            return
        self.pending_role = "system"
        self.ready_for_input = False
        self.status_var.set(f"Running {command_text}...")
        try:
            self.session.send(command_text)
        except Exception as exc:
            self.append_system(f"command_failed: {exc}")
            self.status_var.set("Command failed")

    def _handle_segment(self, segment: str) -> None:
        cleaned = segment.strip()
        if self.pending_role == "assistant":
            model_text = self._clean_model_text(cleaned)
            if model_text:
                self.append_transcript("Falcon", model_text)
        elif self.pending_role == "system":
            if cleaned:
                self.append_transcript("System", cleaned)
        else:
            if cleaned:
                self.append_transcript("System", cleaned)
        self.pending_role = None
        self.ready_for_input = True
        self.status_var.set("Resident ready")

    def _consume_output(self, chunk: str) -> None:
        self.output_buffer += chunk
        while PROMPT_TEXT in self.output_buffer:
            segment, remainder = self.output_buffer.split(PROMPT_TEXT, 1)
            self.output_buffer = remainder
            self._handle_segment(segment)

    def _poll_events(self) -> None:
        while True:
            try:
                event_type, payload = self.event_queue.get_nowait()
            except queue.Empty:
                break
            if event_type == "chunk":
                self._consume_output(str(payload))
            elif event_type == "exit":
                if self.output_buffer.strip():
                    self.append_system(self.output_buffer)
                self.output_buffer = ""
                self.ready_for_input = False
                self.pending_role = None
                self.session = None
                self.status_var.set(f"Process exited ({payload})")
        self.root.after(60, self._poll_events)

    def _on_return(self, event: tk.Event[tk.Text]) -> str:
        if event.state & 0x0001:
            return "break"
        self.send_prompt()
        return "break"

    def _on_shift_return(self, event: tk.Event[tk.Text]) -> str:
        self.input_text.insert("insert", "\n")
        return "break"

    def _on_close(self) -> None:
        self.stop_session()
        self.root.destroy()


def main() -> None:
    root = tk.Tk()
    app = ChatWindow(root)
    if app.model_var.get().strip():
        app.append_system("Resident model detected. Press 'Start Resident Chat' to boot Falcon in-window.")
    else:
        app.append_system("No default GGUF model was found. Pick a model path, then start the resident chat.")
    root.mainloop()


if __name__ == "__main__":
    main()
