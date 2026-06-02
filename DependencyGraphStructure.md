# Dependency Graph Structure for VB-Style C Build System

## Overview

The dependency graph is the core intelligence layer that enables true incremental compilation in the VB-style architecture. Unlike traditional Makefile-based systems that rely on timestamp checking and implicit header dependencies, this system maintains an explicit, queryable graph of unit relationships.

## Graph Representation

The dependency graph is stored in the `unit_dependencies` table and forms a directed graph where:
- **Nodes** = Compilable units (from `units` table)
- **Edges** = Dependencies between units (from `unit_dependencies` table)
- **Edge direction** = From dependent unit → dependency unit (A depends on B means A → B)

## Dependency Types

Each edge has a `dependency_type` that qualifies the relationship:

1. **header_include** - Unit A includes Unit B's header file
2. **function_call** - Unit A calls a function defined in Unit B
3. **type_usage** - Unit A uses a typedef, struct, or enum from Unit B
4. **extern_variable** - Unit A references an extern variable from Unit B
5. **macro_usage** - Unit A uses a macro defined in Unit B
6. **inline_usage** - Unit A uses an inline function from Unit B

## Dependency Strength

Each edge has a `dependency_strength` that determines how changes propagate:

- **strong** - Changes to the dependency always require recompilation of the dependent unit
  - Examples: function signature changes, struct layout changes, enum value changes
  
- **weak** - Changes to the dependency may not require recompilation (depends on change nature)
  - Examples: adding new functions (if not used), adding private members, documentation-only changes

## Graph Properties

1. **Acyclic by Design** - The system should prevent circular dependencies through architectural rules
2. **Transitive Closure Available** - Dependencies can be traversed to find all affected units
3. **Weighted Edges** - Strength values allow for smart recompilation decisions
4. **Timestamped** - Each dependency has a `detected_at` timestamp for tracking when relationships were discovered

## Dependency Detection Process

Dependencies are discovered through static analysis during the registration/compilation process:

1. **Source Scanning** - When a unit is registered or updated, its source is scanned for:
   - `#include` directives → header_include dependencies
   - Function calls → function_call dependencies (requires symbol resolution)
   - Type references → type_usage dependencies
   - Extern declarations → extern_variable dependencies

2. **Interface Analysis** - Header files are parsed to extract:
   - Function signatures
   - Type definitions
   - Macro definitions
   - Extern declarations

3. **Symbol Resolution** - Function calls are matched against known interfaces to create precise dependencies

## Using the Graph for Incremental Builds

When a unit changes, the build system uses the graph to determine what needs rebuilding:

1. **Identify Changed Unit** - Compare source hash and interface hash to detect changes
2. **Forward Traversal** - Find all units that depend (directly or indirectly) on the changed unit
3. **Apply Strength Rules** - 
   - For strong dependencies: always mark dependent for recompilation
   - For weak dependencies: analyze if the specific change affects the dependency
4. **Build Order Generation** - Topological sort of affected units to determine compilation sequence
5. **Selective Compilation** - Only compile units marked as needing rebuild

## Example Query: What Needs Rebuilding?

```sql
-- Find all units that (directly or indirectly) depend on unit X
WITH RECURSIVE dependent_units AS (
    -- Base case: units that directly depend on X
    SELECT dependent_unit_id, dependency_type, dependency_strength
    FROM unit_dependencies 
    WHERE dependency_unit_id = X
    
    UNION
    
    -- Recursive case: units that depend on units that depend on X
    SELECT ud.dependent_unit_id, ud.dependency_type, ud.dependency_strength
    FROM unit_dependencies ud
    INNER JOIN dependent_units du ON ud.dependency_unit_id = du.dependent_unit_id
)
SELECT DISTINCT u.unit_name, u.source_path
FROM dependent_units du
JOIN units u ON du.dependent_unit_id = u.unit_id
WHERE u.is_active = 1;
```

## Example Query: Build Order for Affected Units

```sql
-- Get compilation order for all affected units (simplified topological sort)
WITH RECURSIVE build_order AS (
    -- Start with units that have no dependencies (or only external deps)
    SELECT u.unit_id, u.unit_name, 0 as level
    FROM units u
    LEFT JOIN unit_dependencies ud ON u.unit_id = ud.dependent_unit_id
    WHERE u.is_active = 1 AND ud.dependent_unit_id IS NULL
    
    UNION ALL
    
    -- Add units whose dependencies are all in previous levels
    SELECT u.unit_id, u.unit_name, bo.level + 1
    FROM units u
    INNER JOIN unit_dependencies ud ON u.unit_id = ud.dependent_unit_id
    INNER JOIN build_order bo ON ud.dependency_unit_id = bo.unit_id
    WHERE u.is_active = 1 
      AND NOT EXISTS (
        SELECT 1 FROM unit_dependencies ud2 
        WHERE ud2.dependent_unit_id = u.unit_id 
          AND ud2.dependency_unit_id NOT IN (SELECT unit_id FROM build_order WHERE level <= bo.level)
      )
      AND u.unit_id NOT IN (SELECT unit_id FROM build_order)
)
SELECT unit_name FROM build_order ORDER BY level;
```

## Integration with Compile States

The dependency graph works with `unit_compile_states` to enable smart rebuilding:

1. When a unit is compiled, its compile state stores:
   - `source_hash` - to detect source changes
   - `interface_hash` - to detect interface changes  
   - `dependencies_hash` - composite hash of all dependency states

2. To check if recompilation is needed:
   ```sql
   -- Check if unit needs recompilation based on its own changes
   SELECT 1 FROM unit_compile_states 
   WHERE unit_id = X 
     AND is_current = 1
     AND (source_hash != :new_source_hash OR interface_hash != :new_interface_hash);
   
   -- OR check if any dependency has changed (using dependencies_hash)
   SELECT 1 FROM unit_compile_states 
   WHERE unit_id = X 
     AND is_current = 1
     AND dependencies_hash != :new_dependencies_hash;
   ```

## Preventing Circular Dependencies

The system can detect and prevent circular dependencies:

```sql
-- Detect circular dependencies using recursive CTE
WITH RECURSIVE path_check AS (
    SELECT 
        dependent_unit_id as start_unit,
        dependent_unit_id as current_unit,
        dependency_unit_id as next_unit,
        1 as depth,
        dependent_unit_id || '->' || dependency_unit_id as path
    FROM unit_dependencies
    
    UNION ALL
    
    SELECT 
        pc.start_unit,
        pc.current_unit,
        ud.dependency_unit_id,
        pc.depth + 1,
        pc.path || '->' || ud.dependency_unit_id
    FROM path_check pc
    INNER JOIN unit_dependencies ud ON pc.next_unit = ud.dependent_unit_id
    WHERE pc.depth < 10  -- Prevent infinite recursion
      AND pc.next_unit != ud.dependency_unit_id  -- Avoid immediate self-loops
)
SELECT 
    start_unit as cycle_start,
    path || '->' || start_unit as cycle_path
FROM path_check
WHERE next_unit = start_unit  -- Found a cycle
  AND depth > 1;
```

## Benefits Over Traditional Approaches

1. **Precision** - Knows exactly what depends on what, no over-compilation
2. **Speed** - Only compiles what's actually affected
3. **Correctness** - Eliminates header guessing problems
4. **Visibility** - Graph can be queried, visualized, and debugged
5. **Architectural Enforcement** - Makes dependencies explicit and manageable
6. **Change Impact Analysis** - Can answer "what breaks if I change X?"

## Implementation Notes

1. **Initial Population** - Dependencies are discovered during initial unit registration
2. **Incremental Updates** - When a unit changes, only its dependencies need rescanning
3. **Cache Invalidation** - When dependencies change, the `dependencies_hash` in compile states becomes stale
4. **External Dependencies** - System libraries and external headers can be treated as special units with fixed hashes