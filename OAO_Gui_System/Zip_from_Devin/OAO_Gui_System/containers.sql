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

-- >>> SEED DATA <<<
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
