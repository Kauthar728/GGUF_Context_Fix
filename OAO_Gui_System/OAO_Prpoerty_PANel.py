"""
property_panel.py — the DYNAMIC Visual-Studio-style property window.

Nothing here is hard-coded per component. When an instance is selected the panel
asks the Catalog:
    * which properties apply to this component
    * the effective min / max / default for each
...and BUILDS the editor fields on the fly. Each row shows whether the value is
a DEFAULT (grey/italic) or has been SET (bold), with a reset button to drop the
override. Constraints (min..max) are shown inline so you can SEE them.

The panel does not mutate the model directly; it emits `changed(prop_key, raw)`
and `reset(prop_key)` and lets the window decide when to apply (Live vs Apply).
"""

from __future__ import annotations

from PyQt6.QtCore import Qt, pyqtSignal
from PyQt6.QtGui import QColor
from PyQt6.QtWidgets import (
    QColorDialog,
    QComboBox,
    QDoubleSpinBox,
    QFrame,
    QGridLayout,
    QHBoxLayout,
    QLabel,
    QLineEdit,
    QPushButton,
    QScrollArea,
    QSpinBox,
    QVBoxLayout,
    QWidget,
)

from OAO_model import Catalog, Instance


class _ColorField(QWidget):
    changed = pyqtSignal()

    def __init__(self, value: str, parent=None) -> None:
        super().__init__(parent)
        lay = QHBoxLayout(self)
        lay.setContentsMargins(0, 0, 0, 0)
        lay.setSpacing(4)
        self.edit = QLineEdit(value)
        self.edit.setMaximumWidth(90)
        self.swatch = QFrame()
        self.swatch.setFixedSize(20, 20)
        self.swatch.setFrameShape(QFrame.Shape.Box)
        self.btn = QPushButton("…")
        self.btn.setFixedWidth(26)
        lay.addWidget(self.edit)
        lay.addWidget(self.swatch)
        lay.addWidget(self.btn)
        self._sync()
        self.edit.editingFinished.connect(self._on_edit)
        self.btn.clicked.connect(self._pick)

    def _sync(self) -> None:
        c = QColor(self.edit.text())
        if c.isValid():
            self.swatch.setStyleSheet(f"background:{c.name()};")

    def _on_edit(self) -> None:
        self._sync()
        self.changed.emit()

    def _pick(self) -> None:
        c = QColor(self.edit.text())
        chosen = QColorDialog.getColor(c if c.isValid() else QColor("#000000"), self)
        if chosen.isValid():
            self.edit.setText(chosen.name())
            self._sync()
            self.changed.emit()

    def value(self) -> str:
        return self.edit.text()


class _Row:
    def __init__(self, prop, editor: QWidget, label: QLabel,
                 reset_btn: QPushButton) -> None:
        self.prop = prop
        self.editor = editor
        self.label = label
        self.reset_btn = reset_btn

    def value(self) -> str:
        e = self.editor
        if isinstance(e, QSpinBox):
            return str(e.value())
        if isinstance(e, QDoubleSpinBox):
            return f"{e.value():g}"
        if isinstance(e, QComboBox):
            return e.currentText()
        if isinstance(e, _ColorField):
            return e.value()
        if isinstance(e, QLineEdit):
            return e.text()
        return ""

    def mark(self, is_set: bool) -> None:
        f = self.label.font()
        f.setBold(is_set)
        f.setItalic(not is_set)
        self.label.setFont(f)
        self.label.setStyleSheet("color:#dcdcaa;" if is_set else "color:#9a9a9a;")
        self.reset_btn.setEnabled(is_set)


class PropertyPanel(QWidget):
    changed = pyqtSignal(str, str)   # prop_key, raw value
    reset = pyqtSignal(str)          # prop_key

    def __init__(self, parent=None) -> None:
        super().__init__(parent)
        self.setMinimumWidth(320)
        self._catalog: Catalog | None = None
        self._inst: Instance | None = None
        self._rows: dict[str, _Row] = {}
        self._building = False

        root = QVBoxLayout(self)
        root.setContentsMargins(8, 8, 8, 8)
        root.setSpacing(6)

        self.header = QLabel("No selection")
        self.header.setStyleSheet("font-weight:bold;font-size:13px;color:#e0e0e0;")
        self.header.setWordWrap(True)
        root.addWidget(self.header)

        self.subhead = QLabel("")
        self.subhead.setWordWrap(True)
        self.subhead.setStyleSheet("color:#9a9a9a;font-size:11px;")
        root.addWidget(self.subhead)

        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        scroll.setFrameShape(QFrame.Shape.NoFrame)
        self._body = QWidget()
        self._grid = QGridLayout(self._body)
        self._grid.setContentsMargins(0, 4, 0, 4)
        self._grid.setHorizontalSpacing(8)
        self._grid.setVerticalSpacing(4)
        self._grid.setColumnStretch(1, 1)
        scroll.setWidget(self._body)
        root.addWidget(scroll, 1)

        self.note = QLabel("")
        self.note.setWordWrap(True)
        self.note.setStyleSheet("color:#d7ba7d;font-size:11px;")
        root.addWidget(self.note)

    # -- public ------------------------------------------------------------

    def set_catalog(self, catalog: Catalog) -> None:
        self._catalog = catalog

    def show_instance(self, inst: Instance | None) -> None:
        self._inst = inst
        self._rebuild()

    def set_note(self, text: str) -> None:
        self.note.setText(text)

    # -- build -------------------------------------------------------------

    def _clear_grid(self) -> None:
        while self._grid.count():
            item = self._grid.takeAt(0)
            w = item.widget()
            if w is not None:
                w.deleteLater()
        self._rows.clear()

    def _rebuild(self) -> None:
        self._building = True
        self._clear_grid()
        self.note.setText("")
        cat, inst = self._catalog, self._inst
        if cat is None or inst is None:
            self.header.setText("No selection")
            self.subhead.setText("Select an item in the canvas or list.")
            self._building = False
            return

        comp = cat.components.get(inst.component)
        cat_name = comp.category if comp else "both"
        self.header.setText(f"{inst.name}  ·  {inst.component} (#{inst.id})")
        desc = comp.description if comp else ""
        self.subhead.setText(f"category: {cat_name}    {desc}")

        row = 0
        for p in cat.applicable_props(inst.component):
            emin, emax, _edef, pdef = cat.effective(inst.component, p.key)
            value, is_set = cat.resolve(inst, p.key)
            editor = self._make_editor(pdef, emin, emax, value)

            label = QLabel(p.label + (f" ({p.unit})" if p.unit else ""))
            rng = _range_hint(pdef, emin, emax)
            if rng:
                label.setToolTip(rng)
            reset = QPushButton("⟲")
            reset.setFixedWidth(26)
            reset.setToolTip("Reset to default")
            reset.clicked.connect(lambda _=False, k=p.key: self._on_reset(k))

            self._grid.addWidget(label, row, 0)
            self._grid.addWidget(editor, row, 1)
            self._grid.addWidget(reset, row, 2)

            r = _Row(pdef, editor, label, reset)
            r.mark(is_set)
            self._rows[p.key] = r
            row += 1

        self._grid.setRowStretch(row, 1)
        self._building = False

    def _make_editor(self, pdef, emin, emax, value: str) -> QWidget:
        dt = pdef.datatype
        if dt == "int":
            sb = QSpinBox()
            sb.setKeyboardTracking(False)
            sb.setMinimum(int(emin) if emin is not None else -10**6)
            sb.setMaximum(int(emax) if emax is not None else 10**6)
            if pdef.unit:
                sb.setSuffix(" " + pdef.unit)
            sb.setValue(int(float(value or 0)))
            sb.valueChanged.connect(lambda _=0, k=pdef.key: self._on_change(k))
            return sb
        if dt == "float":
            sb = QDoubleSpinBox()
            sb.setKeyboardTracking(False)
            sb.setDecimals(2)
            sb.setMinimum(emin if emin is not None else -10**6)
            sb.setMaximum(emax if emax is not None else 10**6)
            if pdef.unit:
                sb.setSuffix(" " + pdef.unit)
            sb.setValue(float(value or 0))
            sb.valueChanged.connect(lambda _=0.0, k=pdef.key: self._on_change(k))
            return sb
        if dt == "enum":
            cb = QComboBox()
            cb.addItems(pdef.enum_list())
            idx = cb.findText(value)
            cb.setCurrentIndex(idx if idx >= 0 else 0)
            cb.currentTextChanged.connect(lambda _="", k=pdef.key: self._on_change(k))
            return cb
        if dt == "color":
            cf = _ColorField(value)
            cf.changed.connect(lambda k=pdef.key: self._on_change(k))
            return cf
        le = QLineEdit(value)
        le.editingFinished.connect(lambda k=pdef.key: self._on_change(k))
        return le

    # -- events ------------------------------------------------------------

    def _on_change(self, prop_key: str) -> None:
        if self._building:
            return
        r = self._rows.get(prop_key)
        if r is None:
            return
        r.mark(True)
        self.changed.emit(prop_key, r.value())

    def _on_reset(self, prop_key: str) -> None:
        if self._building:
            return
        self.reset.emit(prop_key)

    def refresh_marks(self) -> None:
        """Re-sync set/default styling + values from the model after apply."""
        cat, inst = self._catalog, self._inst
        if cat is None or inst is None:
            return
        self._building = True
        for key, r in self._rows.items():
            value, is_set = cat.resolve(inst, key)
            _set_editor_value(r.editor, value)
            r.mark(is_set)
        self._building = False


def _set_editor_value(editor: QWidget, value: str) -> None:
    if isinstance(editor, QSpinBox):
        editor.setValue(int(float(value or 0)))
    elif isinstance(editor, QDoubleSpinBox):
        editor.setValue(float(value or 0))
    elif isinstance(editor, QComboBox):
        idx = editor.findText(value)
        if idx >= 0:
            editor.setCurrentIndex(idx)
    elif isinstance(editor, _ColorField):
        editor.edit.setText(value)
        editor._sync()
    elif isinstance(editor, QLineEdit):
        editor.setText(value)


def _range_hint(pdef, emin, emax) -> str:
    if pdef.datatype in ("int", "float"):
        lo = "" if emin is None else f"{emin:g}"
        hi = "" if emax is None else f"{emax:g}"
        if lo or hi:
            return f"range {lo}..{hi} {pdef.unit}".strip()
    if pdef.datatype == "enum":
        return "options: " + ", ".join(pdef.enum_list())
    return ""
