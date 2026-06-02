#!/usr/bin/env python3
"""Check C source files for real compilation errors by parsing signatures."""
import re
import os

SRC_DIR = os.path.dirname(os.path.abspath(__file__))

def parse_header_funcs(filepath):
    """Extract function declarations from a .h file."""
    funcs = {}
    with open(filepath) as f:
        content = f.read()
    # Match: ReturnType func_name(params);
    pattern = r'(\w+(?:\s*\*)?)\s+(\w+)\s*\(([^)]*)\)\s*;'
    for m in re.finditer(pattern, content):
        ret_type = m.group(1).strip()
        name = m.group(2).strip()
        params = m.group(3).strip()
        funcs[name] = {'ret': ret_type, 'params': params, 'file': os.path.basename(filepath)}
    return funcs

def parse_source_funcs(filepath):
    """Extract function definitions from a .c file."""
    funcs = {}
    with open(filepath) as f:
        content = f.read()
    # Match: ReturnType func_name(params) {
    pattern = r'(\w+(?:\s*\*)?)\s+(\w+)\s*\(([^)]*)\)\s*\{'
    for m in re.finditer(pattern, content):
        ret_type = m.group(1).strip()
        name = m.group(2).strip()
        params = m.group(3).strip()
        funcs[name] = {'ret': ret_type, 'params': params, 'file': os.path.basename(filepath)}
    return funcs

def parse_func_calls(filepath):
    """Extract function calls from a .c file."""
    calls = []
    with open(filepath) as f:
        content = f.read()
    # Match: func_name(args)
    pattern = r'(\w+)\s*\(([^)]*)\)\s*;'
    for m in re.finditer(pattern, content):
        name = m.group(1).strip()
        args = m.group(2).strip()
        # Filter out control flow keywords
        if name not in ('if', 'while', 'for', 'switch', 'return', 'sizeof', 'fprintf', 'printf', 'free', 'calloc', 'malloc', 'strdup', 'strcpy', 'strlen', 'strcmp', 'snprintf', 'exit', 'g_object_unref', 'g_application_run'):
            calls.append({'name': name, 'args': args, 'file': os.path.basename(filepath)})
    return calls

def count_args(args_str):
    """Count comma-separated arguments, ignoring commas in nested parens.
    Handles 'void' as zero args (standard C convention)."""
    s = args_str.strip()
    if not s or s == 'void':
        return 0
    depth = 0
    count = 1
    for c in s:
        if c == '(':
            depth += 1
        elif c == ')':
            depth -= 1
        elif c == ',' and depth == 0:
            count += 1
    return count

def main():
    print("=== C Code Signature Checker ===\n")

    # Collect all headers and sources
    headers = {}
    sources = {}
    all_calls = []

    for fname in os.listdir(SRC_DIR):
        if fname.endswith('.h'):
            headers[fname] = parse_header_funcs(os.path.join(SRC_DIR, fname))
        elif fname.endswith('.c'):
            sources[fname] = parse_source_funcs(os.path.join(SRC_DIR, fname))
            all_calls.extend(parse_func_calls(os.path.join(SRC_DIR, fname)))

    # Merge all header declarations
    all_decls = {}
    for hname, funcs in headers.items():
        for name, info in funcs.items():
            if name not in all_decls:
                all_decls[name] = info
            else:
                # Check for conflicting declarations
                if all_decls[name]['params'] != info['params']:
                    print(f"[CONFLICT] '{name}' declared differently:")
                    print(f"  {all_decls[name]['file']}: {all_decls[name]['ret']} {name}({all_decls[name]['params']})")
                    print(f"  {info['file']}: {info['ret']} {name}({info['params']})")

    # Merge all source definitions
    all_defs = {}
    for sname, funcs in sources.items():
        for name, info in funcs.items():
            if name not in all_defs:
                all_defs[name] = info
            else:
                print(f"[DUPLICATE] '{name}' defined in both {all_defs[name]['file']} and {info['file']}")

    # Check: every function called must be declared or defined
    print("--- Checking function calls ---")
    undefined_calls = []
    for call in all_calls:
        name = call['name']
        if name not in all_decls and name not in all_defs:
            # Could be a macro, GTK function, or standard lib - skip known ones
            undefined_calls.append(call)

    if undefined_calls:
        print(f"[INFO] {len(undefined_calls)} calls to undeclared functions (may be macros/GTK/stdlib):")
        for c in undefined_calls[:10]:
            print(f"  {c['file']}: {c['name']}({c['args']})")
    else:
        print("[OK] All called functions are declared or defined")

    # Check: definitions match declarations
    print("\n--- Checking definition vs declaration ---")
    for name, defn in all_defs.items():
        if name in all_decls:
            decl = all_decls[name]
            # Compare parameter counts
            defn_args = count_args(defn['params'])
            decl_args = count_args(decl['params'])
            if defn_args != decl_args:
                print(f"[ERROR] '{name}' parameter count mismatch:")
                print(f"  {decl['file']} (decl): {decl_args} args")
                print(f"  {defn['file']} (def):  {defn_args} args")
            else:
                print(f"[OK] '{name}' signature matches ({defn_args} args)")

    # Check: function calls match declaration arity
    print("\n--- Checking call arity vs declarations ---")
    for call in all_calls:
        name = call['name']
        if name in all_decls:
            decl = all_decls[name]
            call_args = count_args(call['args'])
            decl_args = count_args(decl['params'])
            if call_args != decl_args:
                print(f"[ERROR] '{name}' called with {call_args} args but declared with {decl_args}:")
                print(f"  {call['file']}: {name}({call['args']})")
                print(f"  declared in {decl['file']}: {decl['ret']} {name}({decl['params']})")

    print("\n=== Done ===")

if __name__ == "__main__":
    main()
