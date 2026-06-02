from PyQt6 import QtWidgets, QtCore, QtGui
class Toolbar(QtWidgets.QWidget):
    def __init__(self, config: dict, parent=None):
        super().__init__(parent)
        self.config = config or {}
        self.parent_ref = parent
        self.buttons = {}
        self.actions = {}
        self._build_ui()
    # -------------------------
    # UI BUILD
    # -------------------------
    def _build_ui(self):
        position = self.config.get("position", "left")
        padding = self.config.get("padding", 4)
        margin = self.config.get("margin", 4)
        width = self.config.get("width", 50)
        stylesheet = self.config.get("stylesheet", "")
        if position in ("left", "right"):
            layout = QtWidgets.QVBoxLayout(self)
            self.setFixedWidth(width)
        else:
            layout = QtWidgets.QHBoxLayout(self)
            self.setFixedHeight(width)
        layout.setContentsMargins(margin, margin, margin, margin)
        layout.setSpacing(padding)
        buttons = self.config.get("buttons", [])
        for btn_cfg in buttons:
            self._create_button(layout, btn_cfg)
        layout.addStretch(1)
        if stylesheet:
            self.setStyleSheet(stylesheet)
    # -------------------------
    # BUTTON CREATION
    # -------------------------
    def _create_button(self, layout, cfg: dict):
        btn_id = cfg.get("id", f"btn_{len(self.buttons)}")
        btn = QtWidgets.QPushButton()
        label = cfg.get("label", "")
        tooltip = cfg.get("tooltip", "")
        enabled = cfg.get("enabled", True)
        icon_path = cfg.get("icon", None)
        btn.setText(label)
        btn.setToolTip(tooltip)
        btn.setEnabled(enabled)
        if icon_path:
            try:
                btn.setIcon(QtGui.QIcon(icon_path))
            except Exception:
                pass
        btn.clicked.connect(lambda _, b=btn_id: self._handle_click(b))
        self.buttons[btn_id] = btn
        self.actions[btn_id] = cfg.get("callback", None)
        layout.addWidget(btn)
    # -------------------------
    # CALLBACK SYSTEM
    # -------------------------
    def _handle_click(self, btn_id: str):
        callback = self.actions.get(btn_id)
        if callback is None:
            return
        try:
            if callable(callback):
                callback()
                return
            if isinstance(callback, str) and self.parent_ref:
                func = getattr(self.parent_ref, callback, None)
                if callable(func):
                    func()
        except Exception:
            pass  # silent fail by design
    # -------------------------
    # PUBLIC API
    # -------------------------
    def add_button(self, cfg: dict):
        layout = self.layout()
        self._create_button(layout, cfg)
    def remove_button(self, btn_id: str):
        btn = self.buttons.get(btn_id)
        if not btn:
            return
        btn.setParent(None)
        btn.deleteLater()
        self.buttons.pop(btn_id, None)
        self.actions.pop(btn_id, None)
    def set_enabled(self, btn_id: str, state: bool):
        btn = self.buttons.get(btn_id)
        if btn:
            btn.setEnabled(state)
    def set_label(self, btn_id: str, text: str):
        btn = self.buttons.get(btn_id)
        if btn:
            btn.setText(text)
    def set_icon(self, btn_id: str, icon_path: str):
        btn = self.buttons.get(btn_id)
        if not btn:
            return
        try:
            btn.setIcon(QtGui.QIcon(icon_path))
        except Exception:
            pass