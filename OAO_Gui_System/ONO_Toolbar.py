from PyQt6 import QtWidgets, QtGui, QtCore


class Toolbar(QtWidgets.QWidget):
    """
    VB-STYLE TOOLBAR CORE

    INPUT CONTRACT (STRICT):
        config = (
            position,      # "top" | "bottom" | "left" | "right"
            width,         # int
            padding,       # int
            margin,        # int
            buttons        # tuple of button tuples
        )

    BUTTON CONTRACT:
        (
            id,
            label,
            tooltip,
            callback,   # string method name OR callable
            icon,       # path or None
            enabled     # bool
        )

    OUTPUT:
        - UI instance
        - internal button registry
    """

    def __init__(self, config, parent=None):
        super().__init__(parent)

        self.parent_ref = parent
        self.buttons = {}
        self.actions = {}

        self._load_config(config)
        self._build()

    # -----------------------------------------------------
    # CONFIG LOAD (STRICT UNPACK)
    # -----------------------------------------------------
    def _load_config(self, config):
        try:
            (
                self.position,
                self.width,
                self.padding,
                self.margin,
                self.button_data
            ) = config
        except Exception:
            raise ValueError("Invalid toolbar config tuple")

        if self.position not in ("top", "bottom", "left", "right"):
            raise ValueError("Invalid position")

        if not isinstance(self.button_data, (list, tuple)):
            raise ValueError("Buttons must be tuple/list")

    # -----------------------------------------------------
    # BUILD UI
    # -----------------------------------------------------
    def _build(self):
        if self.position in ("left", "right"):
            layout = QtWidgets.QVBoxLayout(self)
            self.setFixedWidth(self.width)
        else:
            layout = QtWidgets.QHBoxLayout(self)
            self.setFixedHeight(self.width)

        layout.setContentsMargins(self.margin, self.margin, self.margin, self.margin)
        layout.setSpacing(self.padding)

        for b in self.button_data:
            self._create_button(layout, b)

        layout.addStretch(1)

    # -----------------------------------------------------
    # BUTTON CREATION (STRICT CONTRACT)
    # -----------------------------------------------------
    def _create_button(self, layout, b):
        try:
            (
                btn_id,
                label,
                tooltip,
                callback,
                icon,
                enabled
            ) = b
        except Exception:
            raise ValueError(f"Invalid button tuple: {b}")

        btn = QtWidgets.QPushButton()

        btn.setText(label or "")
        btn.setToolTip(tooltip or "")
        btn.setEnabled(bool(enabled))

        if icon:
            try:
                btn.setIcon(QtGui.QIcon(icon))
            except Exception:
                pass

        btn.clicked.connect(lambda _, i=btn_id: self._handle(i))

        self.buttons[btn_id] = btn
        self.actions[btn_id] = callback

        layout.addWidget(btn)

    # -----------------------------------------------------
    # CALLBACK RESOLUTION (NO MAGIC)
    # -----------------------------------------------------
    def _handle(self, btn_id):
        cb = self.actions.get(btn_id)

        if cb is None:
            return

        if callable(cb):
            cb()
            return

        if isinstance(cb, str) and self.parent_ref:
            fn = getattr(self.parent_ref, cb, None)
            if callable(fn):
                fn()

    # -----------------------------------------------------
    # PUBLIC CONTROL API (VB STYLE)
    # -----------------------------------------------------
    def set_enabled(self, btn_id, state: bool):
        btn = self.buttons.get(btn_id)
        if btn:
            btn.setEnabled(state)

    def set_label(self, btn_id, text: str):
        btn = self.buttons.get(btn_id)
        if btn:
            btn.setText(text)

    def set_icon(self, btn_id, icon_path: str):
        btn = self.buttons.get(btn_id)
        if btn:
            try:
                btn.setIcon(QtGui.QIcon(icon_path))
            except Exception:
                pass

    def remove_button(self, btn_id):
        btn = self.buttons.get(btn_id)
        if not btn:
            return
        btn.setParent(None)
        btn.deleteLater()
        self.buttons.pop(btn_id, None)
        self.actions.pop(btn_id, None)