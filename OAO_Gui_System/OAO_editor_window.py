"""
editor_window.py — main window: instance list (left) + live canvas (center) +
dynamic property panel (right), plus the toolbar and the .sql file watcher.

Flow:
  * Select an item (list or canvas) -> dynamic panel rebuilds for it.
  * Edit a field -> Live mode applies instantly; otherwise queued until Apply.
  * Apply/Live/Reset write through the Catalog (validate+clamp) then save the
    .sql file (schema preserved, seed regenerated).
  * A QFileSystemWatcher watches the same .sql file; an external hand-edit
    reloads the canvas within ~150 ms.
"""

from __future__ import annotations

import os

from PyQt6.QtCore import Qt, QTimer
from PyQt6.QtGui import QAction
from PyQt6.QtWidgets import (
    QCheckBox,
    QComboBox,
    QFileDialog,
    QHBoxLayout,
    QInputDialog,
    QLabel,
    QListWidget,
    QListWidgetItem,
    QMainWindow,
    QMenu,
    QMessageBox,
    QPushButton,
    QToolBar,
    QVBoxLayout,
    QWidget,
)

import OAO_storage as storage
from OAO_canvas import Canvas
from OAO_model import Catalog, Instance, Workspace
from OAO_property_panel import PropertyPanel


class EditorWindow(QMainWindow):
    def __init__(self, sql_path: str) -> None:
        super().__init__()
        self.sql_path = os.path.abspath(sql_path)
        self.setWindowTitle("OAO Container Editor v1.3")
        self.resize(1180, 720)

        self.catalog: Catalog | None = None
        self.all_instances: list[Instance] = []   # every workspace's items
        self.workspaces: list[Workspace] = []
        self.active_id: int = 1
        self.current_id: int = -1
        self._pending: dict[str, str] = {}   # queued edits (non-live)
        self._saving = False

        self._build_ui()
        self._build_toolbar()

        storage.ensure_file(self.sql_path)
        self.reload_from_disk()
        self._watch_timer = QTimer(self)
        self._watch_timer.setSingleShot(True)
        self._watch_timer.timeout.connect(self.reload_from_disk)
        self._build_watcher()

    # -- active-workspace view --------------------------------------------

    @property
    def instances(self) -> list[Instance]:
        """The items in the currently-active workspace (what the editor shows)."""
        return [i for i in self.all_instances if i.workspace_id == self.active_id]

    def _active_ws(self) -> Workspace | None:
        return next((w for w in self.workspaces if w.id == self.active_id), None)

    # -- ui ---------------------------------------------------------------

    def _build_ui(self) -> None:
        central = QWidget()
        lay = QHBoxLayout(central)
        lay.setContentsMargins(6, 6, 6, 6)
        lay.setSpacing(6)

        left = QWidget()
        lv = QVBoxLayout(left)
        lv.setContentsMargins(0, 0, 0, 0)
        lv.addWidget(QLabel("Instances"))
        self.list = QListWidget()
        self.list.setMaximumWidth(230)
        self.list.currentItemChanged.connect(self._on_list_select)
        lv.addWidget(self.list, 1)
        lay.addWidget(left)

        self.canvas = Canvas()
        self.canvas.selected.connect(self._on_canvas_select)
        self.canvas.moved.connect(self._on_canvas_moved)
        self.canvas.move_committed.connect(lambda _id: self.save_to_disk())
        lay.addWidget(self.canvas, 1)

        self.panel = PropertyPanel()
        self.panel.changed.connect(self._on_panel_changed)
        self.panel.reset.connect(self._on_panel_reset)
        lay.addWidget(self.panel)

        self.setCentralWidget(central)
        self.status = self.statusBar()

    def _build_toolbar(self) -> None:
        tb = QToolBar("Main")
        tb.setMovable(False)
        self.addToolBar(tb)

        self.new_btn = QPushButton("New ▾")
        self.new_menu = QMenu(self)
        self.new_btn.setMenu(self.new_menu)
        tb.addWidget(self.new_btn)

        del_act = QAction("Delete", self)
        del_act.triggered.connect(self.delete_current)
        tb.addAction(del_act)
        tb.addSeparator()

        load_act = QAction("Load…", self)
        load_act.triggered.connect(self.load_dialog)
        tb.addAction(load_act)
        save_act = QAction("Save", self)
        save_act.triggered.connect(self.save_to_disk)
        tb.addAction(save_act)
        reload_act = QAction("Reload", self)
        reload_act.triggered.connect(self.reload_from_disk)
        tb.addAction(reload_act)
        tb.addSeparator()

        self.live_chk = QCheckBox("Live")
        self.live_chk.setChecked(True)
        tb.addWidget(self.live_chk)
        apply_act = QAction("Apply", self)
        apply_act.triggered.connect(self.apply_pending)
        tb.addAction(apply_act)
        tb.addSeparator()

        # -- workspace (layout preset) controls ---------------------------
        tb.addWidget(QLabel(" Layout: "))
        self.ws_combo = QComboBox()
        self.ws_combo.setMinimumWidth(140)
        self.ws_combo.currentIndexChanged.connect(self._on_ws_combo)
        tb.addWidget(self.ws_combo)

        ws_new = QAction("New layout", self)
        ws_new.triggered.connect(self.new_workspace)
        tb.addAction(ws_new)
        ws_dup = QAction("Duplicate", self)
        ws_dup.triggered.connect(self.duplicate_workspace)
        tb.addAction(ws_dup)
        ws_ren = QAction("Rename", self)
        ws_ren.triggered.connect(self.rename_workspace)
        tb.addAction(ws_ren)
        ws_del = QAction("Delete layout", self)
        ws_del.triggered.connect(self.delete_workspace)
        tb.addAction(ws_del)

    def _rebuild_new_menu(self) -> None:
        self.new_menu.clear()
        if self.catalog is None:
            return
        for name in self.catalog.component_names():
            comp = self.catalog.components[name]
            act = QAction(f"{name}  ({comp.category})", self)
            act.triggered.connect(lambda _=False, n=name: self.new_instance(n))
            self.new_menu.addAction(act)

    # -- watcher ----------------------------------------------------------

    def _build_watcher(self) -> None:
        from PyQt6.QtCore import QFileSystemWatcher
        self.watcher = QFileSystemWatcher(self)
        if os.path.exists(self.sql_path):
            self.watcher.addPath(self.sql_path)
        self.watcher.fileChanged.connect(self._on_file_changed)

    def _on_file_changed(self, _path: str) -> None:
        if self._saving:
            return
        # some editors replace the file; re-add the path then debounce reload
        if self.sql_path not in self.watcher.files():
            if os.path.exists(self.sql_path):
                self.watcher.addPath(self.sql_path)
        self._watch_timer.start(150)

    # -- load / save ------------------------------------------------------

    def reload_from_disk(self) -> None:
        try:
            (self.catalog, self.all_instances,
             self.workspaces, self.active_id) = storage.load(self.sql_path)
        except Exception as exc:  # noqa: BLE001
            self.status.showMessage(f"Load error: {exc}", 6000)
            return
        self.panel.set_catalog(self.catalog)
        self._refresh_ws_combo()
        self.canvas.set_data(self.catalog, self.instances)
        self._rebuild_new_menu()
        self._refresh_list()
        if not any(i.id == self.current_id for i in self.instances):
            self.current_id = self.instances[0].id if self.instances else -1
        self._select(self.current_id)
        ws = self._active_ws()
        self.status.showMessage(
            f"Reloaded {os.path.basename(self.sql_path)} — "
            f"layout '{ws.name if ws else '?'}' ({len(self.instances)} items)", 2500)

    def save_to_disk(self) -> None:
        if self.catalog is None:
            return
        self._saving = True
        try:
            storage.save(self.sql_path, self.catalog,
                         self.all_instances, self.workspaces)
        except Exception as exc:  # noqa: BLE001
            self.status.showMessage(f"Save error: {exc}", 6000)
        finally:
            QTimer.singleShot(300, self._clear_saving)
        if self.sql_path not in self.watcher.files() and os.path.exists(self.sql_path):
            self.watcher.addPath(self.sql_path)
        self.canvas.set_data(self.catalog, self.instances)
        self._refresh_list()

    # -- workspaces (layout presets) --------------------------------------

    def _refresh_ws_combo(self) -> None:
        self.ws_combo.blockSignals(True)
        self.ws_combo.clear()
        for w in sorted(self.workspaces, key=lambda x: x.id):
            self.ws_combo.addItem(w.name, w.id)
            if w.id == self.active_id:
                self.ws_combo.setCurrentIndex(self.ws_combo.count() - 1)
        self.ws_combo.blockSignals(False)

    def _on_ws_combo(self, _idx: int) -> None:
        ws_id = self.ws_combo.currentData()
        if ws_id is None or ws_id == self.active_id:
            return
        self.switch_workspace(int(ws_id))

    def switch_workspace(self, ws_id: int) -> None:
        for w in self.workspaces:
            w.is_active = 1 if w.id == ws_id else 0
        self.active_id = ws_id
        self.current_id = self.instances[0].id if self.instances else -1
        self.save_to_disk()
        self.canvas.set_data(self.catalog, self.instances)
        self._refresh_list()
        self._select(self.current_id)
        ws = self._active_ws()
        self.status.showMessage(
            f"Switched to layout '{ws.name if ws else ws_id}'", 2000)

    def _next_ws_id(self) -> int:
        return max((w.id for w in self.workspaces), default=0) + 1

    def _next_inst_id(self) -> int:
        return max((i.id for i in self.all_instances), default=0) + 1

    def new_workspace(self) -> None:
        name, ok = QInputDialog.getText(self, "New layout", "Layout name:")
        if not ok:
            return
        name = name.strip() or f"Layout {self._next_ws_id()}"
        ws = Workspace(id=self._next_ws_id(), name=name, is_active=0)
        self.workspaces.append(ws)
        self.switch_workspace(ws.id)  # opens the (empty) new layout + saves

    def duplicate_workspace(self) -> None:
        src = self._active_ws()
        if src is None:
            return
        new_ws = Workspace(id=self._next_ws_id(),
                           name=f"{src.name} copy", is_active=0)
        self.workspaces.append(new_ws)
        # deep-copy this workspace's instances under fresh ids, remapping parents
        id_map: dict[int, int] = {}
        src_items = self.instances
        for inst in src_items:
            nid = self._next_inst_id_after(id_map)
            id_map[inst.id] = nid
        for inst in src_items:
            clone = Instance(
                id=id_map[inst.id], name=inst.name, component=inst.component,
                parent_id=id_map.get(inst.parent_id, 0),
                workspace_id=new_ws.id, values=dict(inst.values))
            self.all_instances.append(clone)
        self.switch_workspace(new_ws.id)

    def _next_inst_id_after(self, reserved: dict[int, int]) -> int:
        base = max((i.id for i in self.all_instances), default=0)
        used = set(reserved.values())
        nid = base + 1
        while nid in used:
            nid += 1
        return nid

    def rename_workspace(self) -> None:
        ws = self._active_ws()
        if ws is None:
            return
        name, ok = QInputDialog.getText(
            self, "Rename layout", "Layout name:", text=ws.name)
        if not ok:
            return
        ws.name = name.strip() or ws.name
        self.save_to_disk()
        self._refresh_ws_combo()
        self.status.showMessage(f"Renamed layout to '{ws.name}'", 2000)

    def delete_workspace(self) -> None:
        if len(self.workspaces) <= 1:
            self.status.showMessage("Can't delete the last layout", 2500)
            return
        ws = self._active_ws()
        if ws is None:
            return
        if QMessageBox.question(
            self, "Delete layout",
            f"Delete layout '{ws.name}' and its {len(self.instances)} item(s)?",
        ) != QMessageBox.StandardButton.Yes:
            return
        self.all_instances = [i for i in self.all_instances
                              if i.workspace_id != ws.id]
        self.workspaces = [w for w in self.workspaces if w.id != ws.id]
        self.switch_workspace(self.workspaces[0].id)

    def _clear_saving(self) -> None:
        self._saving = False

    def load_dialog(self) -> None:
        path, _ = QFileDialog.getOpenFileName(
            self, "Load .sql", os.path.dirname(self.sql_path), "SQL (*.sql)")
        if not path:
            return
        if self.sql_path in self.watcher.files():
            self.watcher.removePath(self.sql_path)
        self.sql_path = os.path.abspath(path)
        self.setWindowTitle(f"OAO Container Editor v1.3 — {os.path.basename(path)}")
        self.reload_from_disk()
        self.watcher.addPath(self.sql_path)

    # -- selection --------------------------------------------------------

    def _refresh_list(self) -> None:
        self.list.blockSignals(True)
        self.list.clear()
        for inst in self.instances:
            prefix = "    └ " if inst.parent_id else ""
            item = QListWidgetItem(f"{prefix}{inst.name}  [{inst.component}] #{inst.id}")
            item.setData(Qt.ItemDataRole.UserRole, inst.id)
            self.list.addItem(item)
            if inst.id == self.current_id:
                self.list.setCurrentItem(item)
        self.list.blockSignals(False)

    def _by_id(self, inst_id: int) -> Instance | None:
        for i in self.instances:
            if i.id == inst_id:
                return i
        return None

    def _select(self, inst_id: int) -> None:
        self.current_id = inst_id
        self._pending.clear()
        inst = self._by_id(inst_id)
        self.canvas.set_selected(inst_id)
        self.panel.show_instance(inst)
        for row in range(self.list.count()):
            it = self.list.item(row)
            if it.data(Qt.ItemDataRole.UserRole) == inst_id:
                self.list.blockSignals(True)
                self.list.setCurrentItem(it)
                self.list.blockSignals(False)
                break

    def _on_list_select(self, cur, _prev) -> None:
        if cur is None:
            return
        self._select(cur.data(Qt.ItemDataRole.UserRole))

    def _on_canvas_select(self, inst_id: int) -> None:
        self._select(inst_id)

    # -- editing ----------------------------------------------------------

    def _on_panel_changed(self, prop_key: str, raw: str) -> None:
        if self.live_chk.isChecked():
            self._apply_one(prop_key, raw)
            self.save_to_disk()
            self.panel.refresh_marks()
        else:
            self._pending[prop_key] = raw
            self.status.showMessage(
                f"{len(self._pending)} pending edit(s) — hit Apply", 2000)

    def _apply_one(self, prop_key: str, raw: str) -> None:
        inst = self._by_id(self.current_id)
        if inst is None or self.catalog is None:
            return
        note = self.catalog.set_value(inst, prop_key, raw)
        if prop_key == "name":
            inst.name = inst.values.get("name", inst.name)
        bound_note = self.catalog.enforce_bounds(inst)
        combined = "; ".join(n for n in (note, bound_note) if n)
        self.panel.set_note(combined)

    def apply_pending(self) -> None:
        if not self._pending:
            self.status.showMessage("Nothing pending", 1500)
            return
        for k, v in self._pending.items():
            self._apply_one(k, v)
        self._pending.clear()
        self.save_to_disk()
        self.panel.refresh_marks()

    def _on_panel_reset(self, prop_key: str) -> None:
        inst = self._by_id(self.current_id)
        if inst is None or self.catalog is None:
            return
        self.catalog.clear_value(inst, prop_key)
        if prop_key == "name":
            pass
        self.save_to_disk()
        self.panel.refresh_marks()
        self.status.showMessage(f"{prop_key} reset to default", 2000)

    def _on_canvas_moved(self, inst_id: int, x_pct: float, y_pct: float) -> None:
        inst = self._by_id(inst_id)
        if inst is None or self.catalog is None:
            return
        self.catalog.set_value(inst, "x_pct", f"{x_pct:g}")
        self.catalog.set_value(inst, "y_pct", f"{y_pct:g}")
        self.catalog.enforce_bounds(inst)
        self.canvas.update()
        if inst_id == self.current_id:
            self.panel.refresh_marks()

    # -- new / delete -----------------------------------------------------

    def new_instance(self, component: str) -> None:
        if self.catalog is None:
            return
        new_id = self._next_inst_id()
        comp = self.catalog.components.get(component)
        category = comp.category if comp else "both"

        parent_id = 0
        if category in ("child", "both"):
            sel = self._by_id(self.current_id)
            if sel is not None:
                sel_cat = self.catalog.components.get(sel.component)
                if sel_cat and sel_cat.category in ("parent", "both"):
                    parent_id = sel.id
        if category == "child" and parent_id == 0:
            self.status.showMessage(
                f"'{component}' is a child — placed at top level "
                "(select a parent first to nest it)", 4000)

        inst = Instance(id=new_id, name=f"{component}{new_id}",
                        component=component, parent_id=parent_id,
                        workspace_id=self.active_id)
        inst.values["name"] = inst.name
        self.all_instances.append(inst)
        self.current_id = new_id
        self.save_to_disk()
        self._select(new_id)
        self.status.showMessage(f"New {component} #{new_id}", 2500)

    def delete_current(self) -> None:
        inst = self._by_id(self.current_id)
        if inst is None:
            return
        kids = [i for i in self.instances if i.parent_id == inst.id]
        msg = f"Delete '{inst.name}' (#{inst.id})?"
        if kids:
            msg += f"\nThis also deletes {len(kids)} child item(s)."
        if QMessageBox.question(self, "Delete", msg) != QMessageBox.StandardButton.Yes:
            return
        remove = {inst.id} | {k.id for k in kids}
        self.all_instances = [i for i in self.all_instances if i.id not in remove]
        self.current_id = self.instances[0].id if self.instances else -1
        self.save_to_disk()
        self._select(self.current_id)
