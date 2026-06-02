"""
model.py — v1.1 data model + constraint/resolution layer (the "governance" layer).

This is the heart of the system. It does NOT touch PyQt or SQL. It only knows
about four kinds of things plus a resolver that ties them together:

  PropertyDef          One row in the master property catalog. Describes a
                       property that can exist ANYWHERE in the GUI (name, width,
                       height, margin, colour, ...) plus its GLOBAL constraint
                       (datatype, unit, default, min, max, enum options).

  ComponentDef         A reusable container blueprint ("toolbar", "sidebar",
                       "button", "label", "form"...). Has a description and a
                       CATEGORY: 'parent', 'child', or 'both'.

  ComponentConstraint  Per-component override of a single property: tighter
                       min/max, a different default, or "this property does not
                       apply to this component at all".

  Instance             An actual placed thing. Stores ONLY the property values
                       the user changed ("set" values). Everything else falls
                       back to the component default -> global default.

  Catalog              The resolver. Given a component name + property key it
                       computes the EFFECTIVE default / min / max, lists which
                       properties apply, resolves an instance's value (set vs
                       default) and CLAMPS values so nothing illegal is stored.

The three tiers Wayne described:
    ALL properties     -> PropertyDef rows
    DEFAULT properties -> effective default (global default, optionally
                          overridden per component)
    SET properties     -> Instance.values (only what changed)
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Optional


# ---------------------------------------------------------------------------
# Catalog rows
# ---------------------------------------------------------------------------

@dataclass
class PropertyDef:
    key: str                       # machine key, e.g. "width_pct"
    label: str                     # human label, e.g. "Width"
    grp: str = "general"           # identity / geometry / style
    datatype: str = "text"         # int | float | enum | color | text
    unit: str = ""                 # "" | "%" | "px"
    default_value: str = ""        # stored as text, coerced on use
    global_min: Optional[float] = None
    global_max: Optional[float] = None
    enum_values: str = ""          # comma-separated, for datatype == "enum"
    sort_order: int = 0

    def enum_list(self) -> list[str]:
        return [v.strip() for v in self.enum_values.split(",") if v.strip()]


@dataclass
class ComponentDef:
    name: str                      # "toolbar", "button", ...
    description: str = ""
    category: str = "both"         # parent | child | both


@dataclass
class ComponentConstraint:
    component: str
    prop_key: str
    min_override: Optional[float] = None
    max_override: Optional[float] = None
    default_override: Optional[str] = None
    applies: int = 1               # 0 -> property hidden for this component


@dataclass
class Instance:
    id: int = 0
    name: str = "item"
    component: str = "panel"
    parent_id: int = 0             # 0 == top level (inside the main form)
    workspace_id: int = 1          # which layout preset this item belongs to
    values: dict[str, str] = field(default_factory=dict)  # SET values only


@dataclass
class Workspace:
    """A named layout preset. The whole interface graph for one preset is just
    the set of instances whose workspace_id matches this id."""
    id: int = 1
    name: str = "Default"
    is_active: int = 0             # exactly one workspace is active at a time


@dataclass
class Rect:
    """Plain geometry rectangle (no Qt dependency)."""
    x: float
    y: float
    w: float
    h: float

    def inset(self, m: float) -> "Rect":
        return Rect(self.x + m, self.y + m,
                    max(0.0, self.w - 2 * m), max(0.0, self.h - 2 * m))


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _to_number(text: str, fallback: float = 0.0) -> float:
    try:
        return float(text)
    except (TypeError, ValueError):
        return fallback


def clamp(value: float, lo: Optional[float], hi: Optional[float]) -> float:
    if lo is not None and value < lo:
        value = lo
    if hi is not None and value > hi:
        value = hi
    return value


# ---------------------------------------------------------------------------
# Catalog / resolver
# ---------------------------------------------------------------------------

class Catalog:
    """In-memory view of the catalog tables + the resolution logic."""

    def __init__(
        self,
        property_defs: list[PropertyDef],
        component_defs: list[ComponentDef],
        constraints: list[ComponentConstraint],
    ) -> None:
        self.props: dict[str, PropertyDef] = {p.key: p for p in property_defs}
        self.props_sorted: list[PropertyDef] = sorted(
            property_defs, key=lambda p: (p.sort_order, p.key)
        )
        self.components: dict[str, ComponentDef] = {c.name: c for c in component_defs}
        self.constraints: dict[tuple[str, str], ComponentConstraint] = {
            (c.component, c.prop_key): c for c in constraints
        }

    # -- structural queries ------------------------------------------------

    def component_names(self) -> list[str]:
        return sorted(self.components.keys())

    def applicable_props(self, component: str) -> list[PropertyDef]:
        """Properties that apply to a component, in display order."""
        out: list[PropertyDef] = []
        for p in self.props_sorted:
            con = self.constraints.get((component, p.key))
            if con is not None and con.applies == 0:
                continue
            out.append(p)
        return out

    # -- effective constraint (global tightened by component) --------------

    def effective(self, component: str, prop_key: str):
        """Return (emin, emax, edefault, prop_def) for a component/property."""
        p = self.props[prop_key]
        con = self.constraints.get((component, prop_key))

        emin = p.global_min
        emax = p.global_max
        if con is not None and con.min_override is not None:
            emin = con.min_override if emin is None else max(emin, con.min_override)
        if con is not None and con.max_override is not None:
            emax = con.max_override if emax is None else min(emax, con.max_override)

        edef = p.default_value
        if con is not None and con.default_override not in (None, ""):
            edef = con.default_override
        return emin, emax, edef, p

    # -- instance value resolution ----------------------------------------

    def resolve(self, inst: Instance, prop_key: str) -> tuple[str, bool]:
        """Return (value_text, is_set). is_set == False means it's a default."""
        if prop_key in inst.values and inst.values[prop_key] != "":
            return inst.values[prop_key], True
        _, _, edef, _ = self.effective(inst.component, prop_key)
        return edef, False

    def resolved_number(self, inst: Instance, prop_key: str) -> float:
        text, _ = self.resolve(inst, prop_key)
        emin, emax, _, _ = self.effective(inst.component, prop_key)
        return clamp(_to_number(text), emin, emax)

    def resolved_text(self, inst: Instance, prop_key: str) -> str:
        text, _ = self.resolve(inst, prop_key)
        return text

    # -- writing a value with validation ----------------------------------

    def set_value(self, inst: Instance, prop_key: str, raw: str) -> Optional[str]:
        """Validate+clamp a raw value and store it as a SET value on inst.

        Returns a human-readable note if the value was adjusted, else None.
        Storing the value equal to the default still counts as 'set' so the
        user's explicit choice is preserved.
        """
        if prop_key not in self.props:
            return f"unknown property '{prop_key}' ignored"
        emin, emax, _, p = self.effective(inst.component, prop_key)
        note: Optional[str] = None

        if p.datatype in ("int", "float"):
            n = _to_number(raw)
            c = clamp(n, emin, emax)
            if c != n:
                note = f"{p.label} clamped {n:g}->{c:g} ({_range_text(emin, emax)})"
            stored = f"{int(round(c))}" if p.datatype == "int" else f"{c:g}"
            inst.values[prop_key] = stored
        elif p.datatype == "enum":
            options = p.enum_list()
            if options and raw not in options:
                note = f"{p.label} '{raw}' invalid -> {options[0]}"
                raw = options[0]
            inst.values[prop_key] = raw
        elif p.datatype == "color":
            inst.values[prop_key] = _safe_color(raw)
        else:  # text
            inst.values[prop_key] = raw
        return note

    def clear_value(self, inst: Instance, prop_key: str) -> None:
        """Reset a property back to default (remove the SET value)."""
        inst.values.pop(prop_key, None)

    def enforce_bounds(self, inst: Instance) -> Optional[str]:
        """Cross-property rule: a `free` item cannot overflow its parent.

        Adjusts the stored x_pct / y_pct so x+width <= 100 and y+height <= 100.
        Returns a human-readable note if anything was moved, else None.
        """
        if "anchor" not in self.props or "x_pct" not in self.props:
            return None
        if self.resolved_text(inst, "anchor") != "free":
            return None
        notes: list[str] = []
        w = self.resolved_number(inst, "width_pct")
        h = self.resolved_number(inst, "height_pct")
        x = self.resolved_number(inst, "x_pct")
        y = self.resolved_number(inst, "y_pct")
        max_x = max(0.0, 100.0 - w)
        max_y = max(0.0, 100.0 - h)
        if x > max_x:
            inst.values["x_pct"] = f"{max_x:g}"
            notes.append(f"x clamped {x:g}->{max_x:g} (no overflow)")
        if y > max_y:
            inst.values["y_pct"] = f"{max_y:g}"
            notes.append(f"y clamped {y:g}->{max_y:g} (no overflow)")
        return "; ".join(notes) if notes else None

    # -- geometry resolution for the renderer -----------------------------

    def geometry(self, inst: Instance) -> dict:
        """Resolve the geometry/style block the canvas needs, all clamped."""
        def num(k: str, d: float = 0.0) -> float:
            if k in self.props:
                return self.resolved_number(inst, k)
            return d

        def txt(k: str, d: str = "") -> str:
            if k in self.props:
                return self.resolved_text(inst, k)
            return d

        disp_name = inst.values.get("name") or inst.name
        return {
            "name": disp_name,
            "anchor": txt("anchor", "free"),
            "align": txt("align", "start"),
            "x_pct": num("x_pct"),
            "y_pct": num("y_pct"),
            "width_pct": num("width_pct", 30.0),
            "height_pct": num("height_pct", 30.0),
            "priority": num("priority", 10.0),
            "margin": num("margin", 4.0),
            "padding": num("padding", 6.0),
            "bg_color": txt("bg_color", "#1e1e1e"),
            "border_color": txt("border_color", "#3f3f46"),
        }

    # -- collision-free docking layout ------------------------------------

    def layout(
        self, instances: list[Instance], form: Rect
    ) -> tuple[dict[int, Rect], dict[int, Rect]]:
        """Resolve every instance to a non-overlapping rectangle.

        Edge-anchored items (top/bottom/left/right) RESERVE space against a
        shrinking "free area", processed in `priority` order (lower number =
        takes precedence = grabs the corner). So a top toolbar with priority 0
        spans the full width and a left sidebar then fits BELOW it; flip the
        priorities and the sidebar spans full height with the toolbar fitting
        beside it. Nothing ever overlaps.

        `center`/`free` items are then placed inside whatever free area is left.
        The same rules apply recursively inside every container (a child docks
        within its parent's content rect).

        Returns (rects, areas):
          rects  -> id -> final drawn Rect for each instance.
          areas  -> id -> the free-area Rect a floating item was placed within
                    (used to map a drag back to x_pct/y_pct).
        """
        childmap: dict[int, list[Instance]] = {}
        for inst in instances:
            childmap.setdefault(inst.parent_id, []).append(inst)

        rects: dict[int, Rect] = {}
        areas: dict[int, Rect] = {}
        EDGES = ("top", "bottom", "left", "right")

        def place(parent_id: int, content: Rect) -> None:
            kids = childmap.get(parent_id, [])
            edge: list[Instance] = []
            floating: list[Instance] = []
            for k in kids:
                a = self.geometry(k)["anchor"]
                (edge if a in EDGES else floating).append(k)

            def prio(k: Instance) -> tuple[float, int]:
                pr = self.geometry(k)["priority"] if "priority" in self.props else 0.0
                return (pr, k.id)

            edge.sort(key=prio)
            free = Rect(content.x, content.y, content.w, content.h)

            for k in edge:
                g = self.geometry(k)
                a = g["anchor"]
                m = g["margin"]
                if a in ("top", "bottom"):
                    th = max(0.0, min(content.h * g["height_pct"] / 100.0, free.h))
                    if a == "top":
                        band = Rect(free.x, free.y, free.w, th)
                        free = Rect(free.x, free.y + th, free.w, free.h - th)
                    else:
                        band = Rect(free.x, free.y + free.h - th, free.w, th)
                        free = Rect(free.x, free.y, free.w, free.h - th)
                else:
                    tw = max(0.0, min(content.w * g["width_pct"] / 100.0, free.w))
                    if a == "left":
                        band = Rect(free.x, free.y, tw, free.h)
                        free = Rect(free.x + tw, free.y, free.w - tw, free.h)
                    else:
                        band = Rect(free.x + free.w - tw, free.y, tw, free.h)
                        free = Rect(free.x, free.y, free.w - tw, free.h)
                item = band.inset(m)
                rects[k.id] = item
                areas[k.id] = band
                place(k.id, item.inset(g["padding"]))

            for k in floating:
                g = self.geometry(k)
                fin = free.inset(g["margin"])
                w = fin.w * g["width_pct"] / 100.0
                h = fin.h * g["height_pct"] / 100.0
                if g["anchor"] == "center":
                    x = fin.x + (fin.w - w) / 2.0
                    y = fin.y + (fin.h - h) / 2.0
                else:  # free
                    x = fin.x + fin.w * g["x_pct"] / 100.0
                    y = fin.y + fin.h * g["y_pct"] / 100.0
                    x = min(max(x, fin.x), fin.x + max(0.0, fin.w - w))
                    y = min(max(y, fin.y), fin.y + max(0.0, fin.h - h))
                item = Rect(x, y, w, h)
                rects[k.id] = item
                areas[k.id] = fin
                place(k.id, item.inset(g["padding"]))

        place(0, form)
        return rects, areas


def _range_text(lo: Optional[float], hi: Optional[float]) -> str:
    if lo is not None and hi is not None:
        return f"min {lo:g}, max {hi:g}"
    if lo is not None:
        return f"min {lo:g}"
    if hi is not None:
        return f"max {hi:g}"
    return "no bounds"


def _safe_color(text: str) -> str:
    t = (text or "").strip()
    if not t:
        return "#000000"
    if not t.startswith("#"):
        t = "#" + t
    body = t[1:]
    if len(body) not in (3, 6) or any(c not in "0123456789abcdefABCDEF" for c in body):
        return "#000000"
    return t.lower()
