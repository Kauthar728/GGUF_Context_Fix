# OAO Container Editor — v1.1

A standalone PyQt6 editor for **named, constrained containers**, backed by a
single `.sql` file. v1.1 adds a **dynamic property catalog**: the property
window builds itself from data, not hard-coded fields.

## The idea

You are not building "a toolbar". You are building a **Named Layout Container
System**: one generic container with a governance (constraint) layer. "toolbar",
"sidebar", "button", "label", "form" are just *blueprints* (presets) of that one
container. PyQt is only the renderer.

## Three tiers of properties (the v1.1 core)

Everything lives in **one** `.sql` file (schema on top, seed `INSERT`s at the
bottom). The seed is read by running the whole file through SQLite — so the day
it becomes a real database, the same file just works.

| Tier | Table | Meaning |
|------|-------|---------|
| **ALL properties** | `property_defs` | every property the GUI can have + its **global** constraint (datatype, unit, default, min, max, enum options) |
| **DEFAULT properties** | `component_defs` + `component_constraints` | reusable blueprints (with **description** + **category**: parent/child/both) that override a property's min/max/default *for that component*, or hide it |
| **SET properties** | `instances` + `instance_values` | a placed item stores **only the values you changed**; everything else falls back to component default → global default |

**Dynamic panel:** select an item and the property window is built on the fly
from the catalog — each field already carries that component's min/max, and each
value is shown as **default** (grey/italic) or **set** (bold) with a reset (⟲).

**Abstracted vs calculated:** the *rules* are abstracted (declared in the
tables); the *geometry* (pixels, clamping, no-overflow) is **calculated** by the
resolver in `model.py` at render time.

## Run

```bash
pip install -r requirements.txt
python OAO_main.py                 # uses ./containers.sql (created if missing)
python OAO_main.py path/to/my.sql  # or point it at any .sql
```

## Layout

```
┌───────────┬──────────────────────────┬───────────────────────┐
│ Instances │  MAIN FORM (live preview) │  Dynamic property panel│
│  list     │  containers drawn inside, │  fields built from the │
│           │  click-select, drag free  │  catalog for the       │
│           │  ones                     │  selected item         │
└───────────┴──────────────────────────┴───────────────────────┘
Toolbar: New ▾ (per blueprint) · Delete · Load · Save · Reload · Live · Apply
```

- **Live** on → every edit applies + saves instantly. Off → edits queue until
  **Apply**.
- The preview **watches the `.sql` file**: hand-edit it in another editor and the
  canvas reloads within ~150 ms.

## Collisions & precedence (docking)

Edge-anchored components (`top` / `bottom` / `left` / `right`) **reserve** space —
they never overlay each other. The `Priority` property decides who wins:

- Lower `Priority` number = **takes precedence** = grabs the corner first.
- The layout processes edge items in priority order against a *shrinking free
  area*: a `top` item reserves a band across the current free width, then the
  free area shrinks downward; a `left` item reserves a band down the current
  free height, then shrinks rightward (same for bottom/right).

So **toolbar priority 0 + sidebar priority 1** → the toolbar spans the full width
and the sidebar fits *below* it. Flip them (**sidebar 0, toolbar 1**) → the
sidebar spans the full height and the toolbar fits *beside* it. Either way
nothing ever overlaps. `center`/`free` items then fill whatever space is left,
and the same rule applies recursively inside every container.

The seed defaults are `toolbar=0`, `sidebar=1`, `statusbar=2`; change any item's
`Priority` in the panel to re-order the docking.

## Workspaces (layout presets)

The DB now stores **named layout presets** — every instance belongs to a
`workspace`, and exactly one workspace is *active* at a time. The toolbar has a
**Layout** dropdown plus **New layout / Duplicate / Rename / Delete layout**:

- Switching the dropdown reloads the canvas with that preset's items (saved live).
- **Duplicate** deep-copies the current layout (fresh instance ids, parents
  remapped) so you can fork "IDE mode" → "IDE mode copy" and tweak.
- All presets live in the *same* one `.sql` file (`workspaces` table +
  `instances.workspace_id`), so the whole interface graph reconstructs from the
  database alone, per preset.

The seed ships two: **IDE mode** (toolbar + sidebar + status + a nested button)
and **Minimal mode** (just a toolbar). This is the foundation for IDE/minimal/
debug presets and undoable layout snapshots.

## SQL Console (paste SQL → reality updates)

`sql_console.py` is the paste-and-run half of the workflow:

```bash
python sql_console.py                 # in-memory scratchpad
python sql_console.py containers.sql  # load the editor's file to inspect
```

- Paste SQL (or **Load .sql**) → **Run** executes it via `executescript()` so
  multi-statement `CREATE`+`INSERT` blocks run as one script (it does NOT split
  on `;`, so semicolons inside strings are safe).
- Pick **Open/Create .db** to persist to a real database file, or **:memory:**
  for a throwaway run. Errors roll back the whole script.
- The **Tables** dropdown + preview grid let you SEE that reality actually
  updated — not just trust a success message.

This is the bridge to "run straight from the DB": paste the one `.sql`, run it
into a `.db`, and your app reads from the database.

## Files

| File | Responsibility |
|------|----------------|
| `OAO_model.py` | dataclasses + `Catalog` resolver (effective min/max/default, clamping, no-overflow). No Qt, no SQL. |
| `OAO_storage.py` | load/save the 5-table `.sql` file via SQLite; preserve schema, regenerate seed. |
| `OAO_canvas.py` | live preview; resolve geometry → pixels; select + drag; parent-governs-child nesting. |
| `OAO_property_panel.py` | dynamic VS-style property grid (default/set/reset, inline ranges). |
| `OAO_editor_window.py` | wiring: list + canvas + panel + toolbar + file watcher. |
| `OAO_main.py` | entry point. |
| `sql_console.py` | standalone paste-SQL → execute → browse-tables console. |
| `containers.sql` | the single source of truth (schema + seed). |

## Constraints enforced

- Sizes clamped to each property's effective min..max (e.g. toolbar height 2–30%).
- No negative margin/padding.
- `free` items can't overflow the parent (x+width ≤ 100, y+height ≤ 100) — the
  stored value is corrected and a note tells you which rule fired.
- Edge-anchored items **reserve** space by `Priority` and never overlap (docking).
- Unknown enum/colour values fall back safely.
