"""
OAO_renderer.py — the RUNTIME half of the system.

The editor (OAO_main.py) is the *authoring* tool: you design named containers
with constraints and it persists them to the single `.sql` spec. This module is
the other end of the same pipe: it reads that spec and builds a **real, running
PyQt window** — actual QToolBar/QFrame/QPushButton/QLabel widgets, positioned by
the very same constraint geometry the editor's canvas previews.

So the loop is:  design in editor -> save spec -> render a live app here ->
(file watch) -> the running app reloads itself when the spec changes.

The geometry here intentionally mirrors OAO_canvas so "what you design is what
runs": each instance resolves to a rectangle inside its parent's content box
(parent minus padding), anchored top/bottom/left/right/center or free-positioned,
and a child can never escape its parent.

Run standalone:
    python OAO_renderer.py containers.sql      # opens the live app + hot reload
"""

from __future__ import annotations

import os
import sys

from PyQt6.QtCore import QRectF, Qt, QTimer
from PyQt6.QtGui import QKeySequence, QShortcut
from PyQt6.QtWidgets import (
    QApplication,
    QFrame,
    QLabel,
    QMainWindow,
    QPushButton,
    QWidget,
)

import OAO_storage as storage
from OAO_model import Catalog, Instance


# ---------------------------------------------------------------------------
# Geometry — mirrors OAO_canvas so the running app matches the editor preview.
# Returns absolute pixel rects (in the form's coordinate space) keyed by id.
# ---------------------------------------------------------------------------

def compute_abs_rects(
    catalog: Catalog, instances: list[Instance], form: QRectF
) -> dict[int, QRectF]:
    rects: dict[int, QRectF] = {}
    by_id = {i.id: i for i in instances}

    def parent_content(inst: Instance) -> QRectF:
        if inst.parent_id and inst.parent_id in rects:
            pr = rects[inst.parent_id]
            parent = by_id.get(inst.parent_id)
            if parent is not None:
                pad = catalog.geometry(parent)["padding"]
                return pr.adjusted(pad, pad, -pad, -pad)
            return pr
        return form

    def compute(g: dict, parent: QRectF) -> QRectF:
        margin = g["margin"]
        inner = parent.adjusted(margin, margin, -margin, -margin)
        pw, ph = inner.width(), inner.height()
        w = pw * g["width_pct"] / 100.0
        h = ph * g["height_pct"] / 100.0
        anchor, align = g["anchor"], g["align"]

        def ax(width: float) -> float:
            if align == "center":
                return inner.left() + (pw - width) / 2.0
            if align == "end":
                return inner.right() - width
            return inner.left()

        def ay(height: float) -> float:
            if align == "center":
                return inner.top() + (ph - height) / 2.0
            if align == "end":
                return inner.bottom() - height
            return inner.top()

        if anchor == "top":
            return QRectF(ax(w), inner.top(), w, h)
        if anchor == "bottom":
            return QRectF(ax(w), inner.bottom() - h, w, h)
        if anchor == "left":
            return QRectF(inner.left(), ay(h), w, h)
        if anchor == "right":
            return QRectF(inner.right() - w, ay(h), w, h)
        if anchor == "center":
            return QRectF(inner.left() + (pw - w) / 2.0,
                          inner.top() + (ph - h) / 2.0, w, h)
        x = inner.left() + pw * g["x_pct"] / 100.0
        y = inner.top() + ph * g["y_pct"] / 100.0
        x = min(max(x, inner.left()), inner.right() - w)
        y = min(max(y, inner.top()), inner.bottom() - h)
        return QRectF(x, y, w, h)

    # parents before children so a child can resolve its parent's rect
    for inst in sorted(instances, key=lambda i: (i.parent_id != 0, i.id)):
        g = catalog.geometry(inst)
        rects[inst.id] = compute(g, parent_content(inst))
    return rects


# ---------------------------------------------------------------------------
# Widget factory — map a component blueprint to a real Qt widget.
# ---------------------------------------------------------------------------

def _contrast_text(bg: str) -> str:
    """Pick black/white text for legibility against a #rrggbb background."""
    c = (bg or "").lstrip("#")
    if len(c) == 3:
        c = "".join(ch * 2 for ch in c)
    try:
        r, g, b = (int(c[i:i + 2], 16) for i in (0, 2, 4))
    except (ValueError, IndexError):
        return "#e0e0e0"
    # relative luminance
    return "#1e1e1e" if (0.299 * r + 0.587 * g + 0.114 * b) > 150 else "#f0f0f0"


def make_widget(parent: QWidget, inst: Instance, g: dict) -> QWidget:
    """Build the real widget for one instance (not yet positioned)."""
    comp = inst.component
    name = g["name"]
    bg = g["bg_color"]
    fg = _contrast_text(bg)
    border = g["border_color"]

    if comp == "button":
        w: QWidget = QPushButton(name, parent)
        w.setStyleSheet(
            f"QPushButton{{background:{bg};color:{fg};border:1px solid {border};"
            f"border-radius:4px;padding:4px;}}"
            f"QPushButton:hover{{border:1px solid #4ec9b0;}}"
        )
    elif comp == "label":
        w = QLabel(name, parent)
        w.setStyleSheet(f"QLabel{{color:{fg};background:{bg};padding:2px;}}")
        w.setAlignment(Qt.AlignmentFlag.AlignCenter)
    else:
        # toolbar / sidebar / panel / form / statusbar / anything else: a frame
        w = QFrame(parent)
        w.setStyleSheet(
            f"QFrame{{background:{bg};border:1px solid {border};border-radius:4px;}}"
        )
    # help layer (data-driven): hover tooltip + contextual statusbar help.
    tip = g.get("tooltip") or ""
    sc = g.get("shortcut") or ""
    base_tip = tip or f"{name}  [{comp}]"
    w.setToolTip(f"{base_tip}  ({sc})" if sc else base_tip)
    help_text = g.get("help") or ""
    if help_text:
        w.setStatusTip(help_text)        # shown in the window status bar on hover/focus
        w.setWhatsThis(help_text)        # Shift+F1 contextual help
    return w


# ---------------------------------------------------------------------------
# The live app
# ---------------------------------------------------------------------------

class RenderedApp(QMainWindow):
    """A real running window built from a catalog + a set of instances.

    Pass instances for a single workspace (the active layout). The window
    rebuilds widget geometry on every resize, so percentage-based layouts stay
    responsive exactly like the editor preview.
    """

    def __init__(self, catalog: Catalog, instances: list[Instance],
                 title: str = "OAO — Live App", parent: QWidget | None = None) -> None:
        super().__init__(parent)
        self.setWindowTitle(title)
        self.resize(900, 620)
        self._catalog = catalog
        self._instances = instances
        self._widgets: dict[int, QWidget] = {}
        self._shortcuts: list[QShortcut] = []

        self._root = QWidget()
        self._root.setStyleSheet("background:#1e1e1e;")
        self.setCentralWidget(self._root)
        self.statusBar().showMessage("Ready")  # hover an item to see its help here
        self._build()

    def set_data(self, catalog: Catalog, instances: list[Instance]) -> None:
        self._catalog = catalog
        self._instances = instances
        self._build()

    def _clear(self) -> None:
        for w in self._widgets.values():
            w.setParent(None)
            w.deleteLater()
        self._widgets.clear()
        for sc in self._shortcuts:
            sc.setParent(None)
            sc.deleteLater()
        self._shortcuts.clear()

    def _build(self) -> None:
        self._clear()
        # parents first so a child can be parented to its container widget
        for inst in sorted(self._instances, key=lambda i: (i.parent_id != 0, i.id)):
            parent_w = self._widgets.get(inst.parent_id, self._root)
            g = self._catalog.geometry(inst)
            w = make_widget(parent_w, inst, g)
            self._widgets[inst.id] = w
            if isinstance(w, QPushButton):
                nm = g["name"]
                w.clicked.connect(
                    lambda _=False, n=nm: self.statusBar().showMessage(
                        f"Clicked: {n}", 2000))
            self._bind_shortcut(inst, g)
        self._relayout()
        for w in self._widgets.values():
            w.show()

    def _bind_shortcut(self, inst: Instance, g: dict) -> None:
        """Wire a keyboard shortcut (from the spec) to the instance's widget:
        a button is clicked, anything else is focused. Data-driven, so editing
        the spec changes the live key bindings on reload."""
        seq = (g.get("shortcut") or "").strip()
        if not seq:
            return
        ks = QKeySequence(seq)
        if ks.isEmpty():
            return
        w = self._widgets.get(inst.id)
        if w is None:
            return
        sc = QShortcut(ks, self)

        def trigger(target: QWidget = w) -> None:
            if isinstance(target, QPushButton):
                target.animateClick()
            else:
                target.setFocus(Qt.FocusReason.ShortcutFocusReason)

        sc.activated.connect(trigger)
        self._shortcuts.append(sc)

    def _relayout(self) -> None:
        if not self._instances:
            return
        form = QRectF(0, 0, self._root.width(), self._root.height())
        abs_rects = compute_abs_rects(self._catalog, self._instances, form)
        for inst in self._instances:
            r = abs_rects.get(inst.id)
            w = self._widgets.get(inst.id)
            if r is None or w is None:
                continue
            # child geometry is relative to its parent widget's top-left
            ox, oy = 0.0, 0.0
            if inst.parent_id and inst.parent_id in abs_rects:
                pr = abs_rects[inst.parent_id]
                ox, oy = pr.left(), pr.top()
            w.setGeometry(int(r.left() - ox), int(r.top() - oy),
                          int(r.width()), int(r.height()))

    def resizeEvent(self, event) -> None:  # noqa: N802
        super().resizeEvent(event)
        self._relayout()


class LiveRenderedApp(RenderedApp):
    """RenderedApp that hot-reloads from a .sql spec when the file changes.

    This is the standalone runtime: the GUI literally changes shape when you
    edit the spec (in the OAO editor or by hand), within ~150 ms.
    """

    def __init__(self, path: str, parent: QWidget | None = None) -> None:
        self._path = os.path.abspath(path)
        catalog, instances, active = _load_active(self._path)
        super().__init__(catalog, instances,
                         title=f"OAO — Live App ({os.path.basename(self._path)})",
                         parent=parent)
        self._reload_timer = QTimer(self)
        self._reload_timer.setSingleShot(True)
        self._reload_timer.timeout.connect(self._reload)
        self._build_watcher()

    def _build_watcher(self) -> None:
        from PyQt6.QtCore import QFileSystemWatcher
        self._watcher = QFileSystemWatcher(self)
        if os.path.exists(self._path):
            self._watcher.addPath(self._path)
        self._watcher.fileChanged.connect(self._on_changed)

    def _on_changed(self, _p: str) -> None:
        if self._path not in self._watcher.files() and os.path.exists(self._path):
            self._watcher.addPath(self._path)
        self._reload_timer.start(150)

    def _reload(self) -> None:
        try:
            catalog, instances, _active = _load_active(self._path)
        except Exception:  # noqa: BLE001
            return
        self.set_data(catalog, instances)


def _load_active(path: str) -> tuple[Catalog, list[Instance], int]:
    """Load a spec and return (catalog, active-workspace instances, active_id)."""
    catalog, all_instances, _workspaces, active_id = storage.load(path)
    instances = [i for i in all_instances if i.workspace_id == active_id]
    return catalog, instances, active_id


def main(argv: list[str] | None = None) -> int:
    argv = sys.argv if argv is None else argv
    path = argv[1] if len(argv) > 1 else "containers.sql"
    storage.ensure_file(path)
    app = QApplication(sys.argv)
    win = LiveRenderedApp(path)
    win.show()
    return app.exec()


if __name__ == "__main__":
    raise SystemExit(main())
