"""
storage.py — v1.1 single-.sql-file persistence.

The .sql file is the ONE source of truth. It has two regions:

    -- schema region:  CREATE TABLE statements (preserved byte-for-byte on save)
    -- >>> SEED DATA <<< marker
    -- seed region:     INSERT statements (regenerated on every save)

To READ the file we execute the WHOLE thing through an in-memory SQLite
database, then SELECT back out. That means the day this file is fed to a real
database, the exact same seed just works — no separate import path.

Five tables make up the system (catalog + instances):

    property_defs          master list of every property + global constraints
    component_defs         reusable blueprints (toolbar/button/...) + category
    component_constraints  per-component min/max/default/applies overrides
    instances              placed items (name, component, parent)
    instance_values        only the SET property values per instance

On save we round-trip ALL of them, so hand-edits to the catalog survive too.
"""

from __future__ import annotations

import os
import sqlite3
import tempfile

from model import (
    Catalog,
    ComponentConstraint,
    ComponentDef,
    Instance,
    PropertyDef,
    Workspace,
)

SEED_MARKER = "-- >>> SEED DATA <<<"

DB_SUFFIXES = (".db", ".sqlite", ".sqlite3")


def is_db_path(path: str) -> bool:
    """A real SQLite database file (run-from-DB mode) vs a .sql text script."""
    return path.lower().endswith(DB_SUFFIXES)


DATA_TABLES = (
    "instance_values", "instances", "workspaces",
    "component_constraints", "component_defs", "property_defs",
)

DEFAULT_SCHEMA = """\
-- =====================================================================
--  OAO GUI System  --  single-file container catalog + instances
--  Schema region. Everything below the SEED DATA marker is regenerated
--  by the editor on save; this region is preserved untouched.
-- =====================================================================

CREATE TABLE IF NOT EXISTS property_defs (
    key           TEXT PRIMARY KEY,   -- machine key (width_pct, bg_color ...)
    label         TEXT NOT NULL,      -- human label
    grp           TEXT NOT NULL,      -- identity | geometry | style
    datatype      TEXT NOT NULL,      -- int | float | enum | color | text
    unit          TEXT DEFAULT '',    -- '' | % | px
    default_value TEXT DEFAULT '',    -- global default (text, coerced on use)
    global_min    REAL,               -- global hard floor (NULL = none)
    global_max    REAL,               -- global hard ceiling (NULL = none)
    enum_values   TEXT DEFAULT '',    -- comma list for datatype=enum
    sort_order    INTEGER DEFAULT 0
);

CREATE TABLE IF NOT EXISTS component_defs (
    name        TEXT PRIMARY KEY,     -- toolbar, sidebar, button, label ...
    description TEXT DEFAULT '',
    category    TEXT DEFAULT 'both'   -- parent | child | both
);

CREATE TABLE IF NOT EXISTS component_constraints (
    component        TEXT NOT NULL,
    prop_key         TEXT NOT NULL,
    min_override     REAL,
    max_override     REAL,
    default_override TEXT,
    applies          INTEGER DEFAULT 1,  -- 0 = property hidden for component
    PRIMARY KEY (component, prop_key)
);

CREATE TABLE IF NOT EXISTS workspaces (
    id        INTEGER PRIMARY KEY,
    name      TEXT NOT NULL,          -- "IDE mode", "Minimal mode", ...
    is_active INTEGER DEFAULT 0       -- exactly one row = 1 (the open layout)
);

CREATE TABLE IF NOT EXISTS instances (
    id           INTEGER PRIMARY KEY,
    name         TEXT NOT NULL,
    component    TEXT NOT NULL,
    parent_id    INTEGER DEFAULT 0,   -- 0 = top level (inside the main form)
    workspace_id INTEGER DEFAULT 1    -- which layout preset owns this item
);

CREATE TABLE IF NOT EXISTS instance_values (
    instance_id INTEGER NOT NULL,
    prop_key    TEXT NOT NULL,
    value       TEXT,
    PRIMARY KEY (instance_id, prop_key)
);
"""


# ---------------------------------------------------------------------------
# Loading
# ---------------------------------------------------------------------------

def load(
    path: str,
) -> tuple[Catalog, list[Instance], list[Workspace], int]:
    """Execute the whole .sql file in memory and read everything back.

    Returns (catalog, all_instances, workspaces, active_id). `all_instances`
    spans every workspace; each instance carries its workspace_id so the editor
    can show one preset at a time while saving them all.

    Works in two modes: a real `.db`/`.sqlite` file is opened directly
    (run-from-database); a `.sql` text file is executed into an in-memory DB.
    """
    if is_db_path(path):
        con = sqlite3.connect(path)
        con.row_factory = sqlite3.Row
    else:
        with open(path, "r", encoding="utf-8") as fh:
            sql_text = fh.read()
        con = sqlite3.connect(":memory:")
        con.row_factory = sqlite3.Row
        con.executescript(sql_text)

    prop_defs = [
        PropertyDef(
            key=r["key"], label=r["label"], grp=r["grp"], datatype=r["datatype"],
            unit=r["unit"] or "", default_value=r["default_value"] or "",
            global_min=r["global_min"], global_max=r["global_max"],
            enum_values=r["enum_values"] or "", sort_order=r["sort_order"] or 0,
        )
        for r in con.execute("SELECT * FROM property_defs")
    ]
    comp_defs = [
        ComponentDef(name=r["name"], description=r["description"] or "",
                     category=r["category"] or "both")
        for r in con.execute("SELECT * FROM component_defs")
    ]
    constraints = [
        ComponentConstraint(
            component=r["component"], prop_key=r["prop_key"],
            min_override=r["min_override"], max_override=r["max_override"],
            default_override=r["default_override"],
            applies=1 if r["applies"] is None else int(r["applies"]),
        )
        for r in con.execute("SELECT * FROM component_constraints")
    ]

    def _cols(table: str) -> set[str]:
        return {r["name"] for r in con.execute(f"PRAGMA table_info({table})")}

    inst_cols = _cols("instances")
    has_ws_col = "workspace_id" in inst_cols

    instances: list[Instance] = []
    for r in con.execute("SELECT * FROM instances ORDER BY id"):
        ws_id = (r["workspace_id"] or 1) if has_ws_col else 1
        inst = Instance(id=r["id"], name=r["name"], component=r["component"],
                        parent_id=r["parent_id"] or 0, workspace_id=ws_id,
                        values={})
        for v in con.execute(
            "SELECT prop_key, value FROM instance_values WHERE instance_id=?",
            (inst.id,),
        ):
            if v["value"] is not None:
                inst.values[v["prop_key"]] = v["value"]
        # name is intrinsic: keep the instances.name column and the 'name'
        # property value in sync (value wins if present).
        if "name" in inst.values:
            inst.name = inst.values["name"]
        else:
            inst.values["name"] = inst.name
        instances.append(inst)

    # workspaces (tolerate older files with no workspaces table)
    workspaces: list[Workspace] = []
    try:
        for r in con.execute("SELECT * FROM workspaces ORDER BY id"):
            workspaces.append(Workspace(id=r["id"], name=r["name"],
                                        is_active=int(r["is_active"] or 0)))
    except sqlite3.OperationalError:
        workspaces = []

    if not workspaces:
        # synthesise a single default workspace covering whatever exists
        workspaces = [Workspace(id=1, name="Default", is_active=1)]
        for inst in instances:
            inst.workspace_id = 1

    active_id = next((w.id for w in workspaces if w.is_active), workspaces[0].id)

    con.close()
    return Catalog(prop_defs, comp_defs, constraints), instances, workspaces, active_id


# ---------------------------------------------------------------------------
# Saving
# ---------------------------------------------------------------------------

def schema_section(sql_text: str) -> str:
    idx = sql_text.find(SEED_MARKER)
    if idx == -1:
        return sql_text.rstrip() + "\n"
    return sql_text[:idx].rstrip() + "\n"


def _q(text: str) -> str:
    return "'" + (text or "").replace("'", "''") + "'"


def _num(value) -> str:
    return "NULL" if value is None else f"{value:g}"


def render_seed(
    catalog: Catalog,
    instances: list[Instance],
    workspaces: list[Workspace] | None = None,
) -> str:
    lines: list[str] = [SEED_MARKER, "--  Regenerated on every save.", ""]

    lines.append("--  property catalog (ALL properties + global constraints)")
    for p in catalog.props_sorted:
        lines.append(
            "INSERT INTO property_defs "
            "(key,label,grp,datatype,unit,default_value,global_min,global_max,"
            "enum_values,sort_order) VALUES ("
            f"{_q(p.key)},{_q(p.label)},{_q(p.grp)},{_q(p.datatype)},"
            f"{_q(p.unit)},{_q(p.default_value)},{_num(p.global_min)},"
            f"{_num(p.global_max)},{_q(p.enum_values)},{p.sort_order});"
        )

    lines.append("")
    lines.append("--  component blueprints (DEFAULT source + category)")
    for c in sorted(catalog.components.values(), key=lambda x: x.name):
        lines.append(
            "INSERT INTO component_defs (name,description,category) VALUES ("
            f"{_q(c.name)},{_q(c.description)},{_q(c.category)});"
        )

    lines.append("")
    lines.append("--  per-component constraint overrides")
    for (comp, key), con in sorted(catalog.constraints.items()):
        lines.append(
            "INSERT INTO component_constraints "
            "(component,prop_key,min_override,max_override,default_override,"
            "applies) VALUES ("
            f"{_q(comp)},{_q(key)},{_num(con.min_override)},"
            f"{_num(con.max_override)},"
            f"{'NULL' if con.default_override is None else _q(con.default_override)},"
            f"{con.applies});"
        )

    if workspaces:
        lines.append("")
        lines.append("--  workspaces (layout presets)")
        for w in sorted(workspaces, key=lambda x: x.id):
            lines.append(
                "INSERT INTO workspaces (id,name,is_active) VALUES ("
                f"{w.id},{_q(w.name)},{int(w.is_active)});"
            )

    lines.append("")
    lines.append("--  instances (placed items) + their SET values")
    for inst in instances:
        lines.append(
            "INSERT INTO instances (id,name,component,parent_id,workspace_id) "
            f"VALUES ({inst.id},{_q(inst.name)},{_q(inst.component)},"
            f"{inst.parent_id},{inst.workspace_id});"
        )
        for key in sorted(inst.values.keys()):
            lines.append(
                "INSERT INTO instance_values (instance_id,prop_key,value) "
                f"VALUES ({inst.id},{_q(key)},{_q(inst.values[key])});"
            )
    lines.append("")
    return "\n".join(lines)


def save(
    path: str,
    catalog: Catalog,
    instances: list[Instance],
    workspaces: list[Workspace] | None = None,
) -> None:
    """Persist everything. For a `.db` file we write rows directly into the
    database (run-from-DB); for a `.sql` file we preserve the schema region and
    rewrite the seed region atomically."""
    if is_db_path(path):
        _save_db(path, catalog, instances, workspaces)
        return

    if os.path.exists(path):
        with open(path, "r", encoding="utf-8") as fh:
            schema = schema_section(fh.read())
    else:
        schema = DEFAULT_SCHEMA

    content = schema.rstrip() + "\n\n" + render_seed(catalog, instances, workspaces)

    folder = os.path.dirname(os.path.abspath(path))
    fd, tmp = tempfile.mkstemp(dir=folder, suffix=".sqltmp")
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as fh:
            fh.write(content)
        os.replace(tmp, path)
    finally:
        if os.path.exists(tmp):
            os.remove(tmp)


def _save_db(
    path: str,
    catalog: Catalog,
    instances: list[Instance],
    workspaces: list[Workspace] | None,
) -> None:
    """Write the full state straight into a real SQLite database file: ensure
    the schema exists, clear the data tables, then re-insert the seed rows."""
    con = sqlite3.connect(path)
    try:
        con.executescript(DEFAULT_SCHEMA)          # CREATE TABLE IF NOT EXISTS
        for table in DATA_TABLES:
            con.execute(f"DELETE FROM {table}")
        con.executescript(render_seed(catalog, instances, workspaces))
        con.commit()
    finally:
        con.close()


def ensure_file(path: str) -> None:
    if os.path.exists(path):
        return
    if is_db_path(path):
        con = sqlite3.connect(path)
        try:
            con.executescript(DEFAULT_SCHEMA)
            con.executescript(STARTER_SEED)
            con.commit()
        finally:
            con.close()
        return
    with open(path, "w", encoding="utf-8") as fh:
        fh.write(DEFAULT_SCHEMA.rstrip() + "\n\n" + STARTER_SEED)


# ---------------------------------------------------------------------------
# Starter seed (used only when the file does not exist yet)
# ---------------------------------------------------------------------------

STARTER_SEED = f"""\
{SEED_MARKER}
--  Regenerated on every save.

--  property catalog (ALL properties + global constraints)
INSERT INTO property_defs (key,label,grp,datatype,unit,default_value,global_min,global_max,enum_values,sort_order) VALUES ('name','Name','identity','text','','item',NULL,NULL,'',0);
INSERT INTO property_defs (key,label,grp,datatype,unit,default_value,global_min,global_max,enum_values,sort_order) VALUES ('anchor','Anchor','geometry','enum','','free',NULL,NULL,'top,bottom,left,right,center,free',10);
INSERT INTO property_defs (key,label,grp,datatype,unit,default_value,global_min,global_max,enum_values,sort_order) VALUES ('align','Align','geometry','enum','','start',NULL,NULL,'start,center,end',11);
INSERT INTO property_defs (key,label,grp,datatype,unit,default_value,global_min,global_max,enum_values,sort_order) VALUES ('x_pct','X','geometry','float','%','0',0,100,'',20);
INSERT INTO property_defs (key,label,grp,datatype,unit,default_value,global_min,global_max,enum_values,sort_order) VALUES ('y_pct','Y','geometry','float','%','0',0,100,'',21);
INSERT INTO property_defs (key,label,grp,datatype,unit,default_value,global_min,global_max,enum_values,sort_order) VALUES ('width_pct','Width','geometry','float','%','30',0,100,'',22);
INSERT INTO property_defs (key,label,grp,datatype,unit,default_value,global_min,global_max,enum_values,sort_order) VALUES ('height_pct','Height','geometry','float','%','30',0,100,'',23);
INSERT INTO property_defs (key,label,grp,datatype,unit,default_value,global_min,global_max,enum_values,sort_order) VALUES ('priority','Priority','geometry','int','','10',0,999,'',24);
INSERT INTO property_defs (key,label,grp,datatype,unit,default_value,global_min,global_max,enum_values,sort_order) VALUES ('margin','Margin','style','int','px','4',0,200,'',30);
INSERT INTO property_defs (key,label,grp,datatype,unit,default_value,global_min,global_max,enum_values,sort_order) VALUES ('padding','Padding','style','int','px','6',0,200,'',31);
INSERT INTO property_defs (key,label,grp,datatype,unit,default_value,global_min,global_max,enum_values,sort_order) VALUES ('bg_color','Background','style','color','','#264f78',NULL,NULL,'',40);
INSERT INTO property_defs (key,label,grp,datatype,unit,default_value,global_min,global_max,enum_values,sort_order) VALUES ('border_color','Border','style','color','','#3f3f46',NULL,NULL,'',41);

--  component blueprints (DEFAULT source + category)
INSERT INTO component_defs (name,description,category) VALUES ('toolbar','Horizontal bar, usually anchored top/bottom full width','parent');
INSERT INTO component_defs (name,description,category) VALUES ('sidebar','Vertical bar anchored left/right full height','parent');
INSERT INTO component_defs (name,description,category) VALUES ('statusbar','Thin bar anchored bottom','parent');
INSERT INTO component_defs (name,description,category) VALUES ('panel','Generic free-floating container','both');
INSERT INTO component_defs (name,description,category) VALUES ('form','Top-level grouping container','parent');
INSERT INTO component_defs (name,description,category) VALUES ('button','Clickable leaf widget','child');
INSERT INTO component_defs (name,description,category) VALUES ('label','Static text leaf widget','child');

--  per-component constraint overrides
INSERT INTO component_constraints (component,prop_key,min_override,max_override,default_override,applies) VALUES ('toolbar','anchor',NULL,NULL,'top',1);
INSERT INTO component_constraints (component,prop_key,min_override,max_override,default_override,applies) VALUES ('toolbar','x_pct',NULL,NULL,NULL,0);
INSERT INTO component_constraints (component,prop_key,min_override,max_override,default_override,applies) VALUES ('toolbar','height_pct',2,30,'8',1);
INSERT INTO component_constraints (component,prop_key,min_override,max_override,default_override,applies) VALUES ('toolbar','width_pct',NULL,NULL,'100',1);
INSERT INTO component_constraints (component,prop_key,min_override,max_override,default_override,applies) VALUES ('toolbar','priority',NULL,NULL,'0',1);
INSERT INTO component_constraints (component,prop_key,min_override,max_override,default_override,applies) VALUES ('sidebar','anchor',NULL,NULL,'left',1);
INSERT INTO component_constraints (component,prop_key,min_override,max_override,default_override,applies) VALUES ('sidebar','width_pct',5,40,'18',1);
INSERT INTO component_constraints (component,prop_key,min_override,max_override,default_override,applies) VALUES ('sidebar','height_pct',NULL,NULL,'100',1);
INSERT INTO component_constraints (component,prop_key,min_override,max_override,default_override,applies) VALUES ('sidebar','priority',NULL,NULL,'1',1);
INSERT INTO component_constraints (component,prop_key,min_override,max_override,default_override,applies) VALUES ('statusbar','anchor',NULL,NULL,'bottom',1);
INSERT INTO component_constraints (component,prop_key,min_override,max_override,default_override,applies) VALUES ('statusbar','height_pct',2,12,'5',1);
INSERT INTO component_constraints (component,prop_key,min_override,max_override,default_override,applies) VALUES ('statusbar','priority',NULL,NULL,'2',1);
INSERT INTO component_constraints (component,prop_key,min_override,max_override,default_override,applies) VALUES ('button','width_pct',2,40,'12',1);
INSERT INTO component_constraints (component,prop_key,min_override,max_override,default_override,applies) VALUES ('button','height_pct',2,30,'8',1);
INSERT INTO component_constraints (component,prop_key,min_override,max_override,default_override,applies) VALUES ('button','bg_color',NULL,NULL,'#0e639c',1);
INSERT INTO component_constraints (component,prop_key,min_override,max_override,default_override,applies) VALUES ('label','width_pct',2,60,'20',1);
INSERT INTO component_constraints (component,prop_key,min_override,max_override,default_override,applies) VALUES ('label','height_pct',2,20,'6',1);

--  workspaces (layout presets)
INSERT INTO workspaces (id,name,is_active) VALUES (1,'IDE mode',1);
INSERT INTO workspaces (id,name,is_active) VALUES (2,'Minimal mode',0);

--  instances (placed items) + their SET values
--  Workspace 1: IDE mode = toolbar + sidebar + status + a nested button.
INSERT INTO instances (id,name,component,parent_id,workspace_id) VALUES (1,'MainToolbar','toolbar',0,1);
INSERT INTO instance_values (instance_id,prop_key,value) VALUES (1,'bg_color','#3c3c3c');
INSERT INTO instances (id,name,component,parent_id,workspace_id) VALUES (2,'LeftNav','sidebar',0,1);
INSERT INTO instances (id,name,component,parent_id,workspace_id) VALUES (3,'Status','statusbar',0,1);
INSERT INTO instances (id,name,component,parent_id,workspace_id) VALUES (4,'SaveBtn','button',1,1);
INSERT INTO instance_values (instance_id,prop_key,value) VALUES (4,'x_pct','2');
INSERT INTO instance_values (instance_id,prop_key,value) VALUES (4,'name','Save');

--  Workspace 2: Minimal mode = just a toolbar.
INSERT INTO instances (id,name,component,parent_id,workspace_id) VALUES (5,'MainToolbar','toolbar',0,2);
INSERT INTO instance_values (instance_id,prop_key,value) VALUES (5,'bg_color','#3c3c3c');
"""
