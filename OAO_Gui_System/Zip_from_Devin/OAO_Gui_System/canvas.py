"""
canvas.py — the live preview. This is the "MAIN FORM": the parent everything
is constrained inside. It draws every instance using the geometry the Catalog
resolves, supports click-to-select, and drag-to-move for `free` items.

The canvas never invents geometry — it asks the Catalog for the resolved,
clamped values and just translates them into pixels. Parent governs child:
a child instance is drawn inside its parent's content rect (parent minus
padding), so it can never escape the parent.
"""

from __future__ import annotations

from PyQt6.QtCore import QRectF, Qt, pyqtSignal
from PyQt6.QtGui import QBrush, QColor, QFont, QPainter, QPen
from PyQt6.QtWidgets import QWidget

from model import Catalog, Instance, Rect


class Canvas(QWidget):
    selected = pyqtSignal(int)               # instance id (or -1)
    moved = pyqtSignal(int, float, float)    # id, x_pct, y_pct (live drag)
    move_committed = pyqtSignal(int)         # id (drag released -> save)

    def __init__(self, parent=None) -> None:
        super().__init__(parent)
        self.setMinimumSize(560, 420)
        self.setMouseTracking(True)
        self._catalog: Catalog | None = None
        self._instances: list[Instance] = []
        self._rects: dict[int, QRectF] = {}
        self._areas: dict[int, Rect] = {}
        self._selected_id: int = -1
        self._drag_id: int = -1
        self._drag_dx: float = 0.0
        self._drag_dy: float = 0.0

    # -- data --------------------------------------------------------------

    def set_data(self, catalog: Catalog, instances: list[Instance]) -> None:
        self._catalog = catalog
        self._instances = instances
        self.update()

    def set_selected(self, inst_id: int) -> None:
        self._selected_id = inst_id
        self.update()

    def _by_id(self, inst_id: int) -> Instance | None:
        for i in self._instances:
            if i.id == inst_id:
                return i
        return None

    # -- geometry ----------------------------------------------------------

    def _form_rect(self) -> QRectF:
        pad = 16.0
        return QRectF(pad, pad, self.width() - 2 * pad, self.height() - 2 * pad)

    def _recompute(self) -> None:
        """Ask the Catalog to resolve a collision-free layout, then translate
        the plain Rects into pixel QRectFs the painter can use."""
        self._rects = {}
        self._areas = {}
        if self._catalog is None:
            return
        fr = self._form_rect()
        form = Rect(fr.left(), fr.top(), fr.width(), fr.height())
        rects, areas = self._catalog.layout(self._instances, form)
        self._areas = areas
        for inst_id, r in rects.items():
            self._rects[inst_id] = QRectF(r.x, r.y, r.w, r.h)

    # -- painting ----------------------------------------------------------

    def paintEvent(self, event) -> None:  # noqa: N802
        p = QPainter(self)
        p.setRenderHint(QPainter.RenderHint.Antialiasing)
        p.fillRect(self.rect(), QColor("#252526"))

        form = self._form_rect()
        p.setPen(QPen(QColor("#555"), 1, Qt.PenStyle.DashLine))
        p.setBrush(QBrush(QColor("#1e1e1e")))
        p.drawRect(form)
        p.setPen(QColor("#777"))
        p.setFont(QFont("Sans", 8))
        p.drawText(form.adjusted(6, 4, 0, 0).topLeft(), "MAIN FORM (parent)")

        if self._catalog is None:
            p.end()
            return

        self._recompute()
        for inst in self._instances:
            rect = self._rects.get(inst.id)
            if rect is None:
                continue
            g = self._catalog.geometry(inst)
            self._draw_instance(p, inst, g, rect)
        p.end()

    def _draw_instance(self, p: QPainter, inst: Instance, g: dict,
                       rect: QRectF) -> None:
        bg = QColor(g["bg_color"])
        if not bg.isValid():
            bg = QColor("#264f78")
        border = QColor(g["border_color"])
        if not border.isValid():
            border = QColor("#3f3f46")

        selected = inst.id == self._selected_id
        p.setBrush(QBrush(bg))
        p.setPen(QPen(QColor("#4ec9b0") if selected else border,
                      2 if selected else 1))
        p.drawRoundedRect(rect, 4, 4)

        # padding visualiser
        pad = g["padding"]
        if pad > 0 and rect.width() > 2 * pad and rect.height() > 2 * pad:
            p.setPen(QPen(QColor(255, 255, 255, 40), 1, Qt.PenStyle.DotLine))
            p.setBrush(Qt.BrushStyle.NoBrush)
            p.drawRect(rect.adjusted(pad, pad, -pad, -pad))

        p.setPen(QColor("#e0e0e0"))
        p.setFont(QFont("Sans", 8, QFont.Weight.Bold))
        label = f"{g['name']}  [{inst.component}]"
        p.drawText(rect.adjusted(6, 4, -4, -4),
                   Qt.AlignmentFlag.AlignLeft | Qt.AlignmentFlag.AlignTop, label)

    # -- interaction -------------------------------------------------------

    def _hit(self, pos) -> int:
        # topmost (children drawn later) wins; iterate reversed
        for inst in reversed(self._instances):
            rect = self._rects.get(inst.id)
            if rect is not None and rect.contains(pos):
                return inst.id
        return -1

    def mousePressEvent(self, event) -> None:  # noqa: N802
        hit = self._hit(event.position())
        self._selected_id = hit
        self.selected.emit(hit)
        self.update()
        if hit != -1 and self._catalog is not None:
            inst = self._by_id(hit)
            if inst is not None and self._catalog.geometry(inst)["anchor"] == "free":
                rect = self._rects[hit]
                self._drag_id = hit
                self._drag_dx = event.position().x() - rect.left()
                self._drag_dy = event.position().y() - rect.top()

    def mouseMoveEvent(self, event) -> None:  # noqa: N802
        if self._drag_id == -1 or self._catalog is None:
            return
        inst = self._by_id(self._drag_id)
        if inst is None:
            return
        area = self._areas.get(self._drag_id)
        if area is None or area.w <= 0 or area.h <= 0:
            return
        new_left = event.position().x() - self._drag_dx
        new_top = event.position().y() - self._drag_dy
        x_pct = (new_left - area.x) / area.w * 100.0
        y_pct = (new_top - area.y) / area.h * 100.0
        self.moved.emit(self._drag_id, x_pct, y_pct)

    def mouseReleaseEvent(self, event) -> None:  # noqa: N802
        if self._drag_id != -1:
            committed = self._drag_id
            self._drag_id = -1
            self.move_committed.emit(committed)
