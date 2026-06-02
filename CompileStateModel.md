# Compile-State Model for VB-Style C Build System

## Overview

The compile-state model tracks the compilation status and metadata for each unit, enabling intelligent incremental builds. This model goes beyond simple timestamp checking by using content-based hashes and tracking interface changes to determine exactly when recompilation is necessary.

## Core Concepts

### 1. Content-Based Change Detection
Instead of relying solely on file timestamps (which can be misleading), the system uses cryptographic hashes to detect actual content changes:
- **Source Hash**: Hash of the .c file content
- **Interface Hash**: Hash of the unit's interface (header file + explicit interface definitions)
- **Dependencies Hash**: Composite hash representing the state of all dependencies

### 2. Three-Layer Hashing Approach
The model uses three distinct hashes to enable precise change detection:

#### Source Hash
- What it covers: The complete content of the unit's .c file
- Purpose: Detects changes to implementation details
- Change impact: Always requires recompilation of this unit
- Example: `sha256(file_content)`

#### Interface Hash
- What it covers: 
  - Header file content (if exists)
  - Explicitly declared interfaces from unit_interfaces table
  - Public function signatures, struct definitions, enum values, etc.
- Purpose: Detects changes that affect other units
- Change impact: Requires recompilation of dependent units (based on dependency strength)
- Example: `sha256(header_content + serialized_public_interfaces)`

#### Dependencies Hash
- What it covers: Composite hash of all direct dependency units' interface hashes
- Purpose: Detects when dependencies have changed in ways that might affect this unit
- Change impact: Requires recompilation if any dependency interface changed (based on dependency strength)
- Example: `sha256(concat(sorted(dependency_interface_hashes)))`

## Compile States Table Details

The `unit_compile_states` table stores the following information for each compilation attempt:

### Primary Fields
- `unit_id`: Foreign key to the units table
- `artifact_path`: Path to the compiled .o file
- `source_hash`: Hash of the .c file at time of compilation
- `interface_hash`: Hash of the unit's interface at time of compilation
- `dependencies_hash`: Hash of dependency states at time of compilation
- `compiler_command`: Full command used (for reproducibility and caching)
- `compiler_version`: Version of compiler used
- `compile_timestamp`: When compilation occurred
- `compile_status`: success/failed/pending
- `error_message`: Error details if compilation failed
- `is_current`: Boolean flag indicating if this is the latest successful compilation

### Hash Calculation Details

#### Source Hash Calculation
```
source_hash = SHA256(.c_file_content)
```
- Updated whenever the .c file changes
- If source_hash differs from stored value → unit needs recompilation

#### Interface Hash Calculation
```
interface_hash = SHA256(
    header_file_content (if exists) +
    serialized_public_interfaces_from_unit_interfaces_table
)
```
Where serialized_public_interfaces includes:
- Function signatures (name, return type, parameters)
- Public struct/union definitions
- Public enum definitions
- Public typedefs
- Public extern variable declarations
- Public macro definitions (if tracked)

#### Dependencies Hash Calculation
```
dependencies_hash = SHA256(
    concat(sorted([
        dependency_unit_interface_hash 
        for each direct dependency
    ]))
)
```
- Updated whenever any direct dependency's interface_hash changes
- If dependencies_hash differs from stored value → unit may need recompilation
- The actual decision depends on dependency strength and change type

## Change Detection Logic

The system uses the following logic to determine if a unit needs recompilation:

### Level 1: Direct Source Change (Always Recompile)
```
IF unit.source_hash != stored_source_hash
    THEN mark unit for recompilation
```

### Level 2: Interface Change (Depends on Dependents)
```
ELSE IF unit.interface_hash != stored_interface_hash
    THEN 
        FOR each unit that depends on this unit:
            IF dependency.dependency_strength = 'strong'
                OR (dependency.dependency_strength = 'weak' AND change affects used interface)
            THEN mark dependent unit for recompilation
```

### Level 3: Dependency Change (Transitive Effect)
```
ELSE IF unit.dependencies_hash != stored_dependencies_hash
    THEN
        // At least one dependency's interface changed
        FOR each direct dependency:
            IF dependency.interface_hash changed:
                // Re-evaluate this unit against the changed dependency
                Apply Level 1 and Level 2 logic using the dependency as the "changed unit"
```

## Handling Different Change Types

### Implementation Changes (Source Hash Only)
- Changes to function bodies
- Changes to private functions (not in interface)
- Changes to comments and formatting
- **Effect**: Only requires recompilation of this unit

### Interface Changes (Interface Hash)
- Changes to function signatures (return type, parameters)
- Changes to public struct layout (field addition/removal/type change)
- Changes to public enum values (addition/removal/value change)
- Changes to public typedefs
- Changes to public extern variable declarations
- **Effect**: May require recompilation of dependent units based on usage and dependency strength

### Dependency Changes (Dependencies Hash)
- Any change that alters a dependency's interface_hash
- **Effect**: Propagates through the dependency graph based on strength rules

## Storing and Retrieving Compile Artifacts

### Artifact Path Strategy
Artifacts are stored in a structured build cache:
```
{build_cache_dir}/{unit_name}/{hash_prefix}.o
```
Where:
- `build_cache_dir` is configurable (default: ./build)
- `unit_name` comes from the units table
- `hash_prefix` is first 8 characters of source_hash (for human readability)

Example: `./build/CASCADE_Coordinator/a1b2c3d4.o`

### Artifact Metadata
Alongside each .o file, metadata is stored:
```
{build_cache_dir}/{unit_name}/{hash_prefix}.o.json
```
Containing:
- Full source_hash
- Full interface_hash  
- Full dependencies_hash
- Compiler command and version
- Compile timestamp
- Any compilation warnings

## Handling Compilation Failures

When compilation fails:
1. A record is still inserted into unit_compile_states
2. `compile_status` is set to 'failed'
3. `error_message` contains the compiler error output
4. `is_current` remains 0 (no current successful compilation)
5. `artifact_path` may be NULL or point to a failed attempt

This allows the system to:
- Track failure history
- Avoid retrying the same failed compilation without changes
- Provide better error reporting

## Cleanup and Garbage Collection

### Old Artifact Removal
The system can clean up old artifacts using:
```
DELETE FROM unit_compile_states 
WHERE unit_id = ? 
  AND is_current = 0 
  AND compile_timestamp < datetime('now', '-30 days')
```
Associated .o files and metadata can be removed based on orphaned records.

### Stale State Detection
Periodically, the system can detect stale compile states:
```sql
-- Find units where stored hashes don't match current files
SELECT u.unit_name
FROM units u
JOIN unit_compile_states ucs ON u.unit_id = ucs.unit_id AND ucs.is_current = 1
WHERE 
    ucs.source_hash != sha256(u.source_path) 
    OR ucs.interface_hash != calculate_interface_hash(u.unit_id)
    OR ucs.dependencies_hash != calculate_dependencies_hash(u.unit_id)
```

## Integration with Build Process

### Compilation Workflow
1. **Check if recompilation needed** (using hash comparisons above)
2. **If needed**:
   - Extract compiler command from configuration
   - Compile unit to temporary location
   - If successful:
     - Move artifact to cache location
     - Create metadata file
     - Insert new record in unit_compile_states
     - Mark previous current record as not current
     - Update unit's timestamps
   - If failed:
     - Insert failed record in unit_compile_states
     - Preserve error message

### Linking Workflow
Before linking, the system:
1. Identifies all units needed for the target executable
2. For each unit, gets the current successful compile state
3. Collects all artifact_paths
4. Invokes linker with those object files
5. Stores link result in build_jobs table

## Benefits of This Model

### Over Timestamp-Based Systems
- Immune to clock skew and timestamp manipulation
- Detects actual semantic changes, not just file touches
- Prevents unnecessary recompilation when formatting changes

### Over Simple Hash-Based Systems
- Separates implementation changes from interface changes
- Enables precise dependency-based recompilation decisions
- Tracks why recompilation was needed (for debugging)

### Over Full Rebuild Systems
- Dramatically reduces build times for incremental changes
- Maintains correctness through precise change tracking
- Scales well to large codebases

## Implementation Considerations

### Hash Algorithm Selection
- SHA256 provides good balance of speed and collision resistance
- Could be made configurable via build_configuration table
- For development, faster non-cryptographic hashes (like xxHash) might be preferred

### Performance Optimization
- Hash calculations can be cached and incrementally updated
- Interface hash calculation can be optimized by only hashing changed parts
- Dependencies hash can be computed incrementally when dependencies change

### Extensibility
- The model can be extended to track:
  - Compilation warnings
  - Test results associated with units
  - Code coverage data
  - Performance benchmarks per unit

This compile-state model provides the foundation for a truly intelligent incremental build system that understands the semantic meaning of changes, not just superficial file modifications.