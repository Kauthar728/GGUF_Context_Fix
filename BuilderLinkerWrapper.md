# Builder/Linker Wrapper for VB-Style C Build System

## Overview

The builder/linker wrapper is the orchestration layer that uses the registry to implement true incremental compilation. Instead of invoking the compiler directly, build processes go through this wrapper which consults the registry to determine what needs to be built, in what order, and how to link the results.

This wrapper transforms C from a language with fragile build semantics into a platform for a managed, dependency-aware build system that behaves like modern runtime environments.

## Core Responsibilities

1. **Change Detection** - Uses the compile-state model to determine what needs recompilation
2. **Dependency Resolution** - Uses the dependency graph to find affected units
3. **Build Planning** - Creates an optimal build order based on dependencies
4. **Compilation Management** - Invokes compilers for only the necessary units
5. **Artifact Management** - Stores and retrieves compiled objects from the registry
6. **Linking Orchestration** - Links only the required artifacts into final executables
7. **Build Tracking** - Records build jobs and their outcomes for auditing and replay

## Architectural Layers

The wrapper consists of several interconnected components:

```
┌─────────────────────────────┐
│   Build Request Interface   │
└─────────────────┬───────────┘
                  │
┌─────────────────▼───────────┐
│   Change Detector           │
└─────────────────┬───────────┘
                  │
┌─────────────────▼───────────┐
│   Dependency Resolver       │
└─────────────────┬───────────┘
                  │
┌─────────────────▼───────────┐
│   Build Planner             │
└─────────────────┬───────────┘
                  │
┌─────────────────▼───────────┐
│   Compiler Invoker          │
└─────────────────┬───────────┘
                  │
┌─────────────────▼───────────┐
│   Artifact Manager          │
└─────────────────┬───────────┘
                  │
┌─────────────────▼───────────┐
│   Linker Orchestrator       │
└─────────────────┬───────────┘
                  │
┌─────────────────▼───────────┐
│   Build Recorder            │
└─────────────────────────────┘
```

## Detailed Component Functions

### 1. Build Request Interface

Accepts build requests from various sources:
- Manual build commands (`build system`, `build unit X`, `rebuild all`)
- File system watchers (auto-rebuild on changes)
- IDE integration (incremental builds during development)
- CI/CD pipelines (automated builds)
- Dependency triggers (when a unit's dependencies change)

Each request includes:
- Trigger reason (manual, file_change, dependency_change, etc.)
- Target units (if specific units requested)
- Build type (debug, release, profile, etc.)
- Force flag (bypass change detection)

### 2. Change Detector

Uses the compile-state model to determine which units actually need recompilation:

```
FOR each unit in the build scope:
    IF unit.force_rebuild OR
       NOT has_current_successful_compile_state(unit) OR
       source_hash_changed(unit) OR
       interface_hash_changed(unit) OR
       dependencies_hash_changed(unit):
        MARK unit as NEEDS_COMPILATION
    ELSE:
        MARK unit as UP_TO_DATE
```

The detector leverages the three-hash system from the compile-state model:
- Source hash changes → always recompile this unit
- Interface hash changes → check dependents based on dependency strength
- Dependencies hash changes → re-evaluate this unit against changed dependencies

### 3. Dependency Resolver

Takes the set of units needing compilation and expands it to include all affected units:

```
affected_units = units_needing_compilation

FOR each unit in units_needing_compilation:
    // Find all units that depend on this unit (transitive closure)
    dependents = get_all_dependents(unit)
    
    FOR each dependent in dependents:
        IF dependency_strength = 'strong' OR
           (dependency_strength = 'weak' AND change_affects_usage):
            ADD dependent to affected_units
```

The resolver uses recursive CTE queries on the `unit_dependencies` table to efficiently compute transitive closures.

### 4. Build Planner

Creates an optimal build order that respects dependencies:

```
build_order = topological_sort(affected_units, dependency_graph)
```

The planner:
- Detects and reports circular dependencies (should be prevented by architecture)
- Groups units that can be compiled in parallel (independent units)
- Considers resource constraints (CPU cores, memory, etc.)
- Optimizes for cache locality when possible
- Generates a build plan with compilation and linking stages

### 5. Compiler Invoker

For each unit in the build plan:
```
FOR each unit in build_order.compilation_stage:
    IF unit.needs_compilation:
        artifact_path = get_compiler_command(unit)
        result = execute_compiler(artifact_path)
        
        IF result.success:
            store_artifact_in_registry(unit, artifact_path, result)
            update_compile_state(unit, result)
        ELSE:
            record_compilation_failure(unit, result.error)
            halt_build_if_critical(unit)
```

The invoker:
- Constructs compiler commands from configuration and unit properties
- Handles compiler-specific flags and optimizations
- Captures compiler output for error reporting and caching
- Manages working directories and include paths
- Supports different compilers (gcc, clang, etc.) via configuration

### 6. Artifact Manager

Handles storage and retrieval of compiled artifacts:

**Storage:**
```
ON successful compilation:
    1. Move .o file to cache location: {build_cache}/{unit_name}/{hash_prefix}.o
    2. Create metadata file: {build_cache}/{unit_name}/{hash_prefix}.o.json
    3. Insert record into unit_compile_states table
    4. Update unit's updated_at timestamp
    5. Mark previous compile states as not current
```

**Retrieval:**
```
WHEN linking needs unit X:
    1. Query unit_compile_states for X where is_current = 1 AND compile_status = 'success'
    2. If found, return artifact_path
    3. If not found, trigger compilation of X
```

The manager also handles:
- Artifact cleanup based on retention policies
- Cache warming for frequently used units
- Handling of compiler-specific artifact formats
- Verification of artifact integrity

### 7. Linker Orchestrator

After all necessary units are compiled:
```
linking_units = get_units_for_target(target_executable)
artifact_paths = [get_artifact_path(unit) for unit in linking_units]

link_command = construct_linker_command(
    artifact_paths,
    target_executable,
    linker_flags_from_config,
    libraries_from_config
)

result = execute_linker(link_command)

IF result.success:
    record_successful_build(target_executable, result)
ELSE:
    record_linking_failure(result.error)
```

The orchestrator:
- Determines which units are needed for a target (executable, library, etc.)
- Respects link order dependencies (though modern linkers are less sensitive)
- Handles library dependencies (both static and dynamic)
- Manages output paths and naming
- Supports different output types (executables, shared libraries, static libraries)

### 8. Build Recorder

Documents every build for traceability and reproducibility:
```
ON build completion:
    1. Create build_jobs record with:
       - trigger_reason, trigger_details
       - start_time, end_time, status
       - units_compiled, units_failed
    2. Create build_job_units records for each unit:
       - status (success/failed/skipped)
       - compile timing information
       - reference to compile state
    3. Optionally store:
       - Compiler and linker version information
       - System information (OS, CPU, memory)
       - Build environment variables
       - Performance metrics
```

This enables:
- Build reproducibility (exact same inputs → same outputs)
- Build performance analysis
- Debugging build failures
- Auditing what changed and why
- Rolling back to previous known-good builds

## Integration with Registry Tables

The wrapper interacts with the registry as follows:

### Units Table
- **Read**: unit_name, source_path, header_path, description, is_active
- **Write**: updated_at (when unit is recompiled or dependencies change)

### UnitInterfaces Table
- **Read**: To build interface_hash for change detection
- **Write**: When interfaces are explicitly defined or discovered

### UnitDependencies Table
- **Read**: For dependency resolution and impact analysis
- **Write**: When new dependencies are discovered during scanning

### UnitCompileStates Table
- **Read**: To determine if recompilation is needed and to retrieve artifacts
- **Write**: After each compilation attempt (success or failure)

### BuildJobs and BuildJobUnits Tables
- **Write**: Complete build history for auditing and replay

### BuildConfiguration Table
- **Read**: To get compiler, flags, cache directories, etc.
- **Write**: When build system configuration changes

## Workflow Examples

### Example 1: Single Unit Change
```
1. User modifies CASCADE_Coordinator.c
2. Build system detects file change
3. Change Detector:
   - Compares source_hash → detects change
   - Marks CASCADE_Coordinator for recompilation
4. Dependency Resolver:
   - Finds units that depend on CASCADE_Coordinator (e.g., CASCADE_Main)
   - Marks them for recompilation based on dependency strength
5. Build Planner:
   - Creates order: [CASCADE_Coordinator, CASCADE_Main] (if Main depends on Coordinator)
6. Compiler Invoker:
   - Compiles CASCADE_Coordinator → stores artifact
   - Compiles CASCADE_Main → stores artifact
7. Linker Orchestrator:
   - Links both artifacts into cascade_workstation executable
8. Build Recorder:
   - Records build job with 2 units compiled, 0 failed
```

### Example 2: Interface Change with Weak Dependency
```
1. User adds a new private function to CASCADE_BackendEngine.c (not in header)
2. Change Detector:
   - Source hash changes → BackendEngine needs recompilation
   - Interface hash unchanged → no interface change detected
3. Dependency Resolver:
   - No dependents marked for recompilation (interface unchanged)
4. Build Planner:
   - Only BackendEngine in build order
5. Compiler Invoker:
   - Compiles BackendEngine
6. Linker Orchestrator:
   - Relinks executable with new BackendEngine artifact
7. Build Recorder:
   - Records build job with 1 unit compiled
```

### Example 3: Header Change Affecting Multiple Units
```
1. User modifies CASCADE_Types.h (changes a public struct used by many units)
2. Change Detector:
   - For each unit that includes CASCADE_Types.h:
     - Interface hash changes → mark unit for recompilation
3. Dependency Resolver:
   - Finds all units that depend on changed units (transitive closure)
   - May mark additional units based on dependency chains
4. Build Planner:
   - Creates optimal compilation order for all affected units
5. Compiler Invoker:
   - Compiles all marked units
6. Linker Orchestrator:
   - Relinks final executable with all updated artifacts
7. Build Recorder:
   - Records build job with N units compiled
```

## Benefits Over Traditional Build Systems

### Compared to Make/Ninja
- **Precision**: Knows exactly what depends on what (no over-compilation from broad header rules)
- **Correctness**: Eliminates header guessing problems that cause incorrect builds
- **Intelligence**: Understands semantic meaning of changes, not just file modifications
- **Traceability**: Complete build history stored in queryable database
- **Flexibility**: Easy to change build policies without rewriting Makefiles

### Compared to Full Rebuild Systems
- **Speed**: Dramatically faster incremental builds (often 10x-100x improvement)
- **Resource Efficiency**: Uses less CPU, memory, and disk I/O
- **Developer Feedback**: Near-instant rebuilds during development
- **Scalability**: Performance doesn't degrade as badly with codebase size

### Compared to IDE-Integrated Builders
- **Consistency**: Same build logic works in IDE, command line, and CI
- **Transparency**: Build process is visible and controllable, not hidden in IDE plugins
- **Portability**: Not tied to specific IDE or platform
- **Extensibility**: Easy to add new languages or build steps

## Implementation Approach

### Phased Rollout
1. **Phase 1**: Registry schema and basic unit tracking
2. **Phase 2**: Change detection and compile-state model
3. **Phase 3**: Dependency discovery and graph building
4. **Phase 4**: Builder/wrapper implementation
5. **Phase 5**: Integration with existing build system
6. **Phase 6**: Optimization and advanced features

### Integration Strategy
- Initially runs alongside existing Makefile build
- Gradually takes over more units as confidence increases
- Provides fallback to traditional build for unsupported cases
- Maintains compatibility with existing developer workflows

### Key Implementation Components
1. **Registry Access Layer** - SQLite interface for all registry operations
2. **Change Detection Engine** - Implements the three-hash logic
3. **Dependency Analyzer** - Static analysis for dependency discovery
4. **Build Scheduler** - Topological sort and parallelization logic
5. **Execution Engine** - Compiler/linker invocation and result handling
6. **CLI Interface** - Command-line interface for build requests
7. **API Interface** - Programmatic interface for IDE/CI integration

## Configuration and Customization

The wrapper is configured through the `build_configuration` table:
- Compiler selection and version requirements
- Compiler and linker flags for different build types
- Cache directory and retention policies
- Dependency tracking sensitivity
- Parallel build limits
- Artifact verification settings
- Notification and reporting preferences

## Conclusion

The builder/linker wrapper transforms the C build process from a fragile, timestamp-based system into a precise, dependency-aware intelligent build system. By leveraging the registry as the source of truth for units, interfaces, dependencies, and compile states, it enables:

1. **Fast incremental builds** - Only recompile what's actually affected
2. **Correct builds** - Eliminate header guessing and implicit dependency problems
3. **Traceable builds** - Complete history of what was built when and why
4. **Reproducible builds** - Same inputs always produce same outputs
5. **Intelligent builds** - Understand the semantic meaning of code changes

This wrapper realizes the VB-style architecture vision where compilation units are truly isolated, dependencies are explicit and tracked, and the build system acts as an intelligent orchestrator rather than a dumb file compiler.