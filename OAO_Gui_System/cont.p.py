#!/usr/bin/env python3
"""
Continuum one-click setup & launcher.

Save this file anywhere and run it:

    python3 continuum_setup.py

What it does, automatically, showing every step on screen:
  1. Downloads the Continuum app (a small zip) from the public link.
  2. Unpacks it into a folder you choose (default: ~/continuum).
  3. Creates an isolated Python environment and installs what it needs.
  4. Starts Continuum and opens it for you.

It prefers a PyQt6 window (with a live log and the app's browser built in).
If PyQt6 isn't available, it still does everything and opens the app in your
normal web browser instead. Nothing here touches your system Python.
"""

import os
import sys
import ssl
import time
import socket
import shutil
import zipfile
import tempfile
import subprocess
import urllib.request
from pathlib import Path






# ----------------------------------------------------------------------------
# Configuration (you can change these in the GUI fields too)
# ----------------------------------------------------------------------------
DEFAULT_URL = "https://continuum-dl-ikwtuxwz.devinapps.com/continuum.zip"
DEFAULT_DIR = str(Path.home() / "continuum")
DEFAULT_PORT = 8000

# Minimal deps so the app at least runs even if the big AI libraries fail.
CORE_DEPS = ["fastapi>=0.110", "uvicorn[standard]>=0.27", "python-multipart>=0.0.9"]


class toolbar:
    ## accpt parameter==position ==top bottom. left right
    ## accpet width of toolbar, als padding , mmrgin, buutton.
    ## button suold have  . icon,lable,tooltip,hover,down,up,enable so each stat can be happy.
    ## then msom how it can now. which button ==. wich   class.method. for click.
    ## all other otions and confg fot toolbar must be available.
    ##. all option and config for buttons must be availabe
    #!!  this way i dont write toolbat ever again  . i just set stylke shee as to say ..

    pass


# ----------------------------------------------------------------------------
# Patch engine layer
# ----------------------------------------------------------------------------
def wrap_code_block(code: str) -> str:
    """Wrap code in triple quotes for safe insertion."""
    return f'"""\n{code}\n"""\n'


def apply_patch(file_path: str, anchor: str, replacement: str, mode: str = "replace") -> None:
    """
    Simple deterministic patch system.
    
    Args:
        file_path: Path to file to patch
        anchor: Text to find in file
        replacement: New content to insert/replace
        mode: 'replace', 'insert_after', or 'insert_before'
    
    Raises:
        ValueError: If anchor not found or invalid mode
    """
    with open(file_path, "r") as f:
        content = f.read()
    
    if anchor not in content:
        raise ValueError(f"Anchor not found in {file_path}")
    
    if mode == "replace":
        new_content = content.replace(anchor, replacement)
    elif mode == "insert_after":
        new_content = content.replace(anchor, anchor + "\n" + replacement)
    elif mode == "insert_before":
        new_content = content.replace(anchor, replacement + "\n" + anchor)
    else:
        raise ValueError(f"Invalid mode: {mode}")
    
    with open(file_path, "w") as f:
        f.write(new_content)


def detect_global_python() -> str | None:
    """Try to find a working system Python on PATH."""
    import shutil
    for name in ("python3", "python"):
        p = shutil.which(name)
        if p:
            return p
    return None


def detect_venv_python(project_root: Path) -> Path | None:
    """Return path to venv python if it exists, else None."""
    vp = _venv_python(project_root)
    return vp if vp.exists() else None


def detect_available_python(install_dir: str) -> str:
    """Auto-detect: prefer venv python if it exists, else global python."""
    project_root = Path(os.path.expanduser(install_dir))
    vp = detect_venv_python(project_root)
    if vp:
        return str(vp)
    gp = detect_global_python()
    if gp:
        return gp
    return sys.executable


# ----------------------------------------------------------------------------
# Core worker functions (used by both the GUI and the console fallback)
# ----------------------------------------------------------------------------
def _venv_python(project_root: Path) -> Path:
    """Path to the python inside the project's virtual environment."""
    if os.name == "nt":
        return project_root / ".venv" / "Scripts" / "python.exe"
    return project_root / ".venv" / "bin" / "python"


def download_zip(url: str, dest: Path, log) -> None:
    log(f"Downloading: {url}")
    ctx = ssl.create_default_context()
    try:
        req = urllib.request.Request(url, headers={"User-Agent": "continuum-setup"})
        with urllib.request.urlopen(req, context=ctx, timeout=120) as r, open(dest, "wb") as f:
            total = int(r.headers.get("Content-Length") or 0)
            got = 0
            while True:
                chunk = r.read(65536)
                if not chunk:
                    break
                f.write(chunk)
                got += len(chunk)
                if total:
                    log(f"  ... {got*100//total}%  ({got//1024} KB)", replace=True)
    except Exception as e:
        raise RuntimeError(f"download failed: {e}")

    # validate it really is a zip (catches 'Unauthorized' html/json pages)
    size = dest.stat().st_size
    with open(dest, "rb") as f:
        head = f.read(2)
    if size < 1000 or head != b"PK":
        raise RuntimeError(
            f"that link did not return a zip (got {size} bytes). "
            f"Check the URL is the public devinapps.com link."
        )
    log(f"Downloaded {size//1024} KB. Looks like a valid zip.")


def extract_into(zip_path: Path, install_dir: Path, log) -> Path:
    log("Unpacking...")
    tmp = Path(tempfile.mkdtemp(prefix="continuum-unz-"))
    with zipfile.ZipFile(zip_path) as z:
        z.extractall(tmp)
    # find the project root (the folder that contains run.py)
    src = tmp
    candidates = [p for p in tmp.iterdir() if p.is_dir()]
    if (tmp / "run.py").exists():
        src = tmp
    elif len(candidates) == 1 and (candidates[0] / "run.py").exists():
        src = candidates[0]
    else:
        for p in candidates:
            if (p / "run.py").exists():
                src = p
                break
    install_dir.mkdir(parents=True, exist_ok=True)
    shutil.copytree(src, install_dir, dirs_exist_ok=True)
    shutil.rmtree(tmp, ignore_errors=True)
    if not (install_dir / "run.py").exists():
        raise RuntimeError("could not find run.py after unpacking")
    log(f"Installed into: {install_dir}")
    return install_dir


def make_venv(project_root: Path, python: str | None = None, log=None) -> None:
    vp = _venv_python(project_root)
    if vp.exists():
        if log:
            log("Virtual environment already exists.")
        return
    py = python or sys.executable
    if log:
        log("Creating virtual environment (.venv)...")
    subprocess.run([py, "-m", "venv", str(project_root / ".venv")], check=True)
    if log:
        log("Upgrading pip...")
    subprocess.run([str(vp), "-m", "pip", "install", "-q", "--upgrade", "pip"], check=False)


def pip_install(project_root: Path, install_ai: bool, python: str | None = None, log=None) -> None:
    vp = _venv_python(project_root)
    py = python or str(vp)
    if install_ai:
        if log:
            log("Installing all dependencies including AI features.")
            log("This is a big one-time download (~1-2 GB) - please be patient.")
        rc = subprocess.run(
            [py, "-m", "pip", "install", "-r", str(project_root / "requirements.txt")]
        ).returncode
        if rc != 0:
            if log:
                log("AI install hit a snag - falling back to the core (no-AI) install so the app still runs.")
            subprocess.run([py, "-m", "pip", "install", *CORE_DEPS], check=True)
    else:
        if log:
            log("Installing core dependencies (no AI libraries).")
        subprocess.run([py, "-m", "pip", "install", *CORE_DEPS], check=True)
    if log:
        log("Dependencies installed.")


def port_is_up(port: int) -> bool:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.settimeout(0.5)
        return s.connect_ex(("127.0.0.1", port)) == 0


def launch_server(project_root: Path, port: int, python: str | None = None, log=None):
    vp = _venv_python(project_root)
    py = python or str(vp)
    if log:
        log(f"Starting Continuum on port {port}...")
    proc = subprocess.Popen(
        [py, "run.py", "--no-browser", "--port", str(port)],
        cwd=str(project_root),
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )
    for _ in range(120):
        if proc.poll() is not None:
            out = proc.stdout.read() if proc.stdout else ""
            raise RuntimeError(f"server exited early:\n{out[-800:]}")
        if port_is_up(port):
            if log:
                log("Continuum is up.")
            return proc
        time.sleep(0.5)
    raise RuntimeError("server did not come up in time")


def full_setup(url: str, install_dir: str, install_ai: bool, port: int, log, python: str | None = None):
    install_path = Path(os.path.expanduser(install_dir))
    tmpzip = Path(tempfile.mkdtemp(prefix="continuum-zip-")) / "continuum.zip"
    download_zip(url, tmpzip, log)
    root = extract_into(tmpzip, install_path, log)
    make_venv(root, python=python, log=log)
    pip_install(root, install_ai, python=python, log=log)
    proc = launch_server(root, port, python=python, log=log)
    return root, proc


# ----------------------------------------------------------------------------
# Console fallback (no PyQt6 needed)
# ----------------------------------------------------------------------------
def run_console(url, install_dir, install_ai, port):
    import webbrowser

    def log(msg, replace=False):
        end = "\r" if replace else "\n"
        print(msg, end=end, flush=True)
        if not replace:
            pass

    print("=" * 60)
    print("  Continuum setup (console mode)")
    print("=" * 60)
    try:
        root, proc = full_setup(url, install_dir, install_ai, port, log)
    except Exception as e:
        print(f"\nSETUP FAILED: {e}")
        sys.exit(1)
    u = f"http://127.0.0.1:{port}"
    print(f"\nOpening {u}")
    try:
        webbrowser.open(u)
    except Exception:
        pass
    print("Continuum is running. Press Ctrl+C here to stop it.")
    try:
        proc.wait()
    except KeyboardInterrupt:
        print("\nStopping...")
        proc.terminate()


# ----------------------------------------------------------------------------
# PyQt6 self-bootstrap: make sure PyQt6 is importable, else install into a
# private launcher venv and re-exec; if that fails, drop to console mode.
# ----------------------------------------------------------------------------
def ensure_pyqt_or_console(url, install_dir, install_ai, port):
    try:
        import PyQt6  # noqa: F401
        return  # GUI available
    except Exception:
        pass

    if os.environ.get("CONTINUUM_BOOTSTRAPPED") == "1":
        # we already tried installing PyQt6 and still can't import it
        print("PyQt6 unavailable; continuing in console mode.")
        run_console(url, install_dir, install_ai, port)
        sys.exit(0)

    print("Setting up the window toolkit (PyQt6) - one-time, small download...")
    launcher = Path.home() / ".continuum_launcher"
    lvenv = launcher / "venv"
    lpy = lvenv / ("Scripts/python.exe" if os.name == "nt" else "bin/python")
    try:
        if not lpy.exists():
            subprocess.run([sys.executable, "-m", "venv", str(lvenv)], check=True)
            subprocess.run([str(lpy), "-m", "pip", "install", "-q", "--upgrade", "pip"], check=False)
        # QtWidgets is required; WebEngine is a nice-to-have (may be skipped).
        subprocess.run([str(lpy), "-m", "pip", "install", "-q", "PyQt6"], check=True)
        subprocess.run([str(lpy), "-m", "pip", "install", "-q", "PyQt6-WebEngine"], check=False)
        env = dict(os.environ, CONTINUUM_BOOTSTRAPPED="1")
        os.execve(str(lpy), [str(lpy), os.path.abspath(__file__), *sys.argv[1:]], env)
    except Exception as e:
        print(f"Could not set up PyQt6 ({e}). Continuing in console mode.")
        run_console(url, install_dir, install_ai, port)
        sys.exit(0)


# ----------------------------------------------------------------------------
# PyQt6 GUI
# ----------------------------------------------------------------------------
def run_gui(url, install_dir, install_ai, port):
    from PyQt6 import QtCore, QtWidgets, QtGui

    try:
        from PyQt6.QtWebEngineWidgets import QWebEngineView
        HAVE_WEB = True
    except Exception:
        HAVE_WEB = False

    class Worker(QtCore.QThread):
        line = QtCore.pyqtSignal(str, bool)
        done = QtCore.pyqtSignal(bool, str)

        def __init__(self, url, install_dir, install_ai, port):
            super().__init__()
            self.url, self.install_dir = url, install_dir
            self.install_ai, self.port = install_ai, port
            self.proc = None

        def run(self):
            def log(msg, replace=False):
                self.line.emit(msg, replace)
            try:
                _root, self.proc = full_setup(
                    self.url, self.install_dir, self.install_ai, self.port, log
                )
                self.done.emit(True, f"http://127.0.0.1:{self.port}")
            except Exception as e:
                self.done.emit(False, str(e))

    class Win(QtWidgets.QMainWindow):
        def __init__(self):
            super().__init__()
            self.setWindowTitle("Continuum - setup & launcher")
            self.resize(1100, 760)
            self.proc = None
            self.worker = None

            central = QtWidgets.QWidget()
            self.setCentralWidget(central)
            outer = QtWidgets.QVBoxLayout(central)

            # --- horizontal layout: sidebar + content ---
            main_split = QtWidgets.QHBoxLayout()
            outer.addLayout(main_split)

            # --- LEFT SIDEBAR ---
            sidebar = QtWidgets.QWidget()
            sidebar_layout = QtWidgets.QVBoxLayout(sidebar)
            sidebar_layout.setContentsMargins(5, 5, 5, 5)
            
            sidebar_layout.addWidget(QtWidgets.QLabel("Tools"))
            
            self.btn_installer = QtWidgets.QPushButton()
            self.btn_installer.setText("📦")
            self.btn_installer.setToolTip("Installer")
            self.btn_installer.setCheckable(True)
            self.btn_installer.setChecked(True)
            self.btn_installer.setFixedSize(40, 40)
            self.btn_installer.clicked.connect(lambda: self.switch_panel(0))
            sidebar_layout.addWidget(self.btn_installer)
            
            self.btn_environment = QtWidgets.QPushButton()
            self.btn_environment.setText("⚡")
            self.btn_environment.setToolTip("Settings")
            self.btn_environment.setCheckable(True)
            self.btn_environment.setFixedSize(40, 40)
            self.btn_environment.clicked.connect(lambda: self.switch_panel(1))
            sidebar_layout.addWidget(self.btn_environment)
            
            self.btn_patch = QtWidgets.QPushButton()
            self.btn_patch.setText("🔧")
            self.btn_patch.setToolTip("Patch Tool")
            self.btn_patch.setCheckable(True)
            self.btn_patch.setFixedSize(40, 40)
            self.btn_patch.clicked.connect(lambda: self.switch_panel(2))
            sidebar_layout.addWidget(self.btn_patch)
            
            sidebar_layout.addStretch(1)
            
            # Setup button with gear icon
            from PyQt6.QtGui import QAction
            self.btn_setup = QtWidgets.QPushButton()
            self.btn_setup.setText("⚙️")
            self.btn_setup.setToolTip("Setup and Configuration")
            self.btn_setup.setFixedSize(40, 40)
            sidebar_layout.addWidget(self.btn_setup)
            
            main_split.addWidget(sidebar, 1)

            # --- RIGHT CONTENT AREA (stacked widget) ---
            self.content_stack = QtWidgets.QStackedWidget()
            main_split.addWidget(self.content_stack, 4)

            # --- PANEL 1: INSTALLER ---
            install_panel = QtWidgets.QWidget()
            install_layout = QtWidgets.QVBoxLayout(install_panel)
            
            form = QtWidgets.QGridLayout()
            form.addWidget(QtWidgets.QLabel("Download link"), 0, 0)
            self.url_in = QtWidgets.QLineEdit(url)
            form.addWidget(self.url_in, 0, 1, 1, 3)

            form.addWidget(QtWidgets.QLabel("Install folder"), 1, 0)
            self.dir_in = QtWidgets.QLineEdit(install_dir)
            form.addWidget(self.dir_in, 1, 1, 1, 2)
            browse = QtWidgets.QPushButton("Choose...")
            browse.clicked.connect(self.pick_dir)
            form.addWidget(browse, 1, 3)

            self.ai_chk = QtWidgets.QCheckBox(
                "Install AI features (semantic search + training) - large one-time download"
            )
            self.ai_chk.setChecked(install_ai)
            form.addWidget(self.ai_chk, 2, 1, 1, 3)
            install_layout.addLayout(form)

            btns = QtWidgets.QHBoxLayout()
            self.go = QtWidgets.QPushButton("Install & Launch")
            self.go.clicked.connect(self.start)
            self.stop_btn = QtWidgets.QPushButton("Stop")
            self.stop_btn.clicked.connect(self.stop_server)
            self.stop_btn.setEnabled(False)
            self.open_btn = QtWidgets.QPushButton("Open Browser")
            self.open_btn.clicked.connect(self.open_external)
            self.open_btn.setEnabled(False)
            btns.addWidget(self.go)
            btns.addWidget(self.stop_btn)
            btns.addWidget(self.open_btn)
            btns.addStretch(1)
            install_layout.addLayout(btns)
            
            if HAVE_WEB:
                self.web = QWebEngineView()
                install_layout.addWidget(self.web)
            else:
                self.web = None
            
            self.content_stack.addWidget(install_panel)

            # --- PANEL 2: SETTINGS ---
            settings_panel = QtWidgets.QWidget()
            settings_layout = QtWidgets.QVBoxLayout(settings_panel)
            
            settings_layout.addWidget(QtWidgets.QLabel("Python Interpreter"))
            self.python_path = QtWidgets.QLineEdit()
            self.python_path.setPlaceholderText("python3 / python or venv path")
            settings_layout.addWidget(self.python_path)
            
            btn_row = QtWidgets.QHBoxLayout()
            detect_btn = QtWidgets.QPushButton("Auto-Detect")
            detect_btn.clicked.connect(self.auto_detect_python)
            btn_row.addWidget(detect_btn)
            browse_py_btn = QtWidgets.QPushButton("Browse...")
            browse_py_btn.clicked.connect(self.pick_python)
            btn_row.addWidget(browse_py_btn)
            btn_row.addStretch(1)
            settings_layout.addLayout(btn_row)
            
            form2 = QtWidgets.QGridLayout()
            form2.addWidget(QtWidgets.QLabel("Install folder"), 0, 0)
            self.dir_in2 = QtWidgets.QLineEdit(install_dir)
            form2.addWidget(self.dir_in2, 0, 1)
            browse2 = QtWidgets.QPushButton("Choose...")
            browse2.clicked.connect(lambda: self.pick_dir2())
            form2.addWidget(browse2, 0, 2)
            settings_layout.addLayout(form2)
            
            form3 = QtWidgets.QGridLayout()
            form3.addWidget(QtWidgets.QLabel("Port"), 1, 0)
            self.port_in = QtWidgets.QSpinBox()
            self.port_in.setRange(1024, 65535)
            self.port_in.setValue(port)
            form3.addWidget(self.port_in, 1, 1)
            settings_layout.addLayout(form3)
            
            self.ai_chk2 = QtWidgets.QCheckBox(
                "Install AI features (semantic search + training) - large one-time download"
            )
            self.ai_chk2.setChecked(install_ai)
            settings_layout.addWidget(self.ai_chk2)
            
            settings_layout.addStretch(1)
            self.content_stack.addWidget(settings_panel)

            # --- PANEL 3: PATCH TOOL ---
            patch_panel = QtWidgets.QWidget()
            patch_layout = QtWidgets.QVBoxLayout(patch_panel)
            
            self.file_btn = QtWidgets.QPushButton("Select File")
            self.file_btn.clicked.connect(self.pick_file)
            patch_layout.addWidget(self.file_btn)
            
            patch_layout.addWidget(QtWidgets.QLabel("Anchor Text (to find)"))
            self.anchor_in = QtWidgets.QLineEdit()
            self.anchor_in.setPlaceholderText("Text to search for in file")
            patch_layout.addWidget(self.anchor_in)
            
            patch_layout.addWidget(QtWidgets.QLabel("Replacement Code"))
            self.code_box = QtWidgets.QPlainTextEdit()
            self.code_box.setPlaceholderText("Code to insert (will be wrapped in triple quotes)")
            patch_layout.addWidget(self.code_box)
            
            self.apply_patch_btn = QtWidgets.QPushButton("Apply Patch")
            self.apply_patch_btn.clicked.connect(self.run_patch)
            patch_layout.addWidget(self.apply_patch_btn)
            
            patch_layout.addStretch(1)
            self.content_stack.addWidget(patch_panel)

            # --- LOG PANEL (visible across all panels) ---
            outer.addWidget(QtWidgets.QLabel("Live Log"))
            self.log = QtWidgets.QPlainTextEdit()
            self.log.setReadOnly(True)
            self.log.setMaximumBlockCount(5000)
            mono = QtGui.QFont("Menlo")
            mono.setStyleHint(QtGui.QFont.StyleHint.Monospace)
            self.log.setFont(mono)
            outer.addWidget(self.log, 1)

            self.status = self.statusBar()
            self.status.showMessage("Ready. Select a tab to begin.")
            self._last_replace = False

        # ---- helpers ----
        def switch_panel(self, index):
            self.content_stack.setCurrentIndex(index)
            self.btn_installer.setChecked(index == 0)
            self.btn_environment.setChecked(index == 1)
            self.btn_patch.setChecked(index == 2)

        def auto_detect_python(self):
            install_dir = self.dir_in2.text().strip() or str(Path.home() / "continuum")
            detected = detect_available_python(install_dir)
            self.python_path.setText(detected)
            self.append(f"Auto-detected Python: {detected}")

        def pick_python(self):
            file_path, _ = QtWidgets.QFileDialog.getOpenFileName(self, "Select Python interpreter")
            if file_path:
                self.python_path.setText(file_path)
                self.append(f"Selected Python: {file_path}")

        def pick_dir(self):
            d = QtWidgets.QFileDialog.getExistingDirectory(self, "Choose install folder")
            if d:
                self.dir_in.setText(str(Path(d) / "continuum"))

        def pick_dir2(self):
            d = QtWidgets.QFileDialog.getExistingDirectory(self, "Choose install folder")
            if d:
                self.dir_in2.setText(str(Path(d) / "continuum"))
                self.dir_in.setText(str(Path(d) / "continuum"))

        def pick_file(self):
            file_path, _ = QtWidgets.QFileDialog.getOpenFileName(self, "Select file")
            if file_path:
                self.selected_file = file_path
                self.append(f"Selected file: {file_path}")

        def append(self, msg, replace=False):
            if replace and self._last_replace:
                # overwrite last line
                cur = self.log.toPlainText().rsplit("\n", 1)[0]
                self.log.setPlainText(cur + ("\n" if cur else "") + msg)
            else:
                self.log.appendPlainText(msg)
            self._last_replace = replace
            self.log.verticalScrollBar().setValue(self.log.verticalScrollBar().maximum())
            self.status.showMessage(msg.strip()[:120])

        def start(self):
            self.go.setEnabled(False)
            self.append("Starting setup...")
            self.worker = Worker(
                self.url_in.text().strip(),
                self.dir_in.text().strip(),
                self.ai_chk.isChecked(),
                port,
            )
            self.worker.line.connect(self.append)
            self.worker.done.connect(self.finished)
            self.worker.start()

        def finished(self, ok, info):
            if not ok:
                self.append("FAILED: " + info)
                QtWidgets.QMessageBox.critical(self, "Setup failed", info)
                self.go.setEnabled(True)
                return
            self.proc = self.worker.proc
            self.app_url = info
            self.append("Ready -> " + info)
            self.stop_btn.setEnabled(True)
            self.open_btn.setEnabled(True)
            if self.web is not None:
                self.web.load(QtCore.QUrl(info))
            else:
                self.open_external()

        def open_external(self):
            import webbrowser
            webbrowser.open(getattr(self, "app_url", f"http://127.0.0.1:{port}"))

        def stop_server(self):
            if self.proc and self.proc.poll() is None:
                self.proc.terminate()
                self.append("Server stopped.")
            self.stop_btn.setEnabled(False)
            self.go.setEnabled(True)

        def run_patch(self):
            if not hasattr(self, 'selected_file'):
                self.append("No file selected. Click 'Select File' first.")
                return
            anchor = self.anchor_in.text()
            if not anchor:
                self.append("No anchor text provided.")
                return
            code = self.code_box.toPlainText()
            if not code:
                self.append("No replacement code provided.")
                return
            wrapped = wrap_code_block(code)
            try:
                apply_patch(self.selected_file, anchor, wrapped)
                self.append("Patch applied successfully")
            except Exception as e:
                self.append(f"Patch failed: {e}")

        def closeEvent(self, e):
            self.stop_server()
            e.accept()

    app = QtWidgets.QApplication(sys.argv)
    w = Win()
    w.show()
    if os.environ.get("CONTINUUM_SELFTEST") == "1":
        # build the window, paint one frame, then exit (used for validation only)
        QtCore.QTimer.singleShot(0, app.quit)
        app.exec()
        print("SELFTEST OK: window built (web=%s)" % HAVE_WEB)
        return
    sys.exit(app.exec())


# ----------------------------------------------------------------------------
# Entry point
# ----------------------------------------------------------------------------
def main():
    import argparse
    ap = argparse.ArgumentParser(description="Continuum setup & launcher")
    ap.add_argument("--url", default=DEFAULT_URL)
    ap.add_argument("--dir", default=DEFAULT_DIR)
    ap.add_argument("--port", type=int, default=DEFAULT_PORT)
    ap.add_argument("--no-ai", action="store_true", help="skip the large AI libraries")
    ap.add_argument("--console", action="store_true", help="force console mode (no window)")
    ap.add_argument("--patch", action="store_true", help="apply a code patch")
    ap.add_argument("--file", type=str, default=None, help="file to patch")
    ap.add_argument("--anchor", type=str, default=None, help="anchor text to find")
    ap.add_argument("--code", type=str, default=None, help="replacement code")
    ap.add_argument("--mode", type=str, default="replace", help="patch mode: replace, insert_after, insert_before")
    args = ap.parse_args()

    install_ai = not args.no_ai

    if args.patch:
        if not args.file or not args.anchor or not args.code:
            print("Missing --file --anchor --code for patch mode")
            sys.exit(1)
        code = wrap_code_block(args.code)
        try:
            apply_patch(args.file, args.anchor, code, mode=args.mode)
            print("Patch applied successfully.")
        except Exception as e:
            print(f"Patch failed: {e}")
            sys.exit(1)
        return

    if args.console:
        run_console(args.url, args.dir, install_ai, args.port)
        return

    ensure_pyqt_or_console(args.url, args.dir, install_ai, args.port)
    # if we get here, PyQt6 is importable
    run_gui(args.url, args.dir, install_ai, args.port)


if __name__ == "__main__":
    main()
