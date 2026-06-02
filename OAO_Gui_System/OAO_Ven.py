#!/usr/bin/env python3
"""
OAO_Ven.py - Virtual Environment Manager

A tool to manage Python virtual environments: list, create, remove, activate.
Also can detect system/global environment variables related to venvs.
"""

import os
import sys
import subprocess
import shutil
import argparse
from pathlib import Path

def get_venv_home():
    """Return the directory where virtual environments are stored.
    Checks common environment variables and defaults."""
    # Check for WORKON_HOME (virtualenvwrapper)
    workon_home = os.environ.get('WORKON_HOME')
    if workon_home:
        return Path(workon_home).expanduser()
    # Check for VENV_PATH (custom)
    venv_path = os.environ.get('VENV_PATH')
    if venv_path:
        return Path(venv_path).expanduser()
    # Default to ~/.virtualenvs
    default = Path.home() / '.virtualenvs'
    if default.exists():
        return default
    # Fallback to ~/venvs
    fallback = Path.home() / 'venvs'
    if fallback.exists():
        return fallback
    # Otherwise, return the default (will be created if needed)
    return default

def list_venvs():
    """List all virtual environments in VENV_HOME."""
    venv_home = get_venv_home()
    if not venv_home.exists():
        print(f"No virtual environments found in {venv_home}")
        return []
    venvs = []
    for entry in venv_home.iterdir():
        if entry.is_dir():
            # Check for typical venv markers
            if (entry / 'bin' / 'activate').exists() or (entry / 'Scripts' / 'activate.bat').exists():
                venvs.append(entry.name)
    if venvs:
        print("Available virtual environments:")
        for v in sorted(venvs):
            print(f"  {v}")
    else:
        print(f"No virtual environments found in {venv_home}")
    return venvs

def create_venv(name, path=None, python=None):
    """Create a new virtual environment."""
    if python is None:
        # Use the user's global Python as specified
        python = "/Users/wws/.local/bin/python"
    if not Path(python).exists():
        print(f"Error: Python interpreter '{python}' not found.")
        return False
    if path:
        target = Path(path).expanduser().resolve()
    else:
        venv_home = get_venv_home()
        venv_home.mkdir(parents=True, exist_ok=True)
        target = venv_home / name
    if target.exists():
        print(f"Error: {target} already exists.")
        return False
    try:
        # Ensure parent directory exists
        target.parent.mkdir(parents=True, exist_ok=True)
        subprocess.run([python, '-m', 'venv', str(target)], check=True)
        print(f"Created virtual environment at {target} using {python}")
        return True
    except subprocess.CalledProcessError as e:
        print(f"Failed to create virtual environment: {e}")
        return False

def remove_venv(name):
    """Remove a virtual environment."""
    venv_home = get_venv_home()
    target = venv_home / name
    if not target.exists():
        # Also check if given as absolute path
        target = Path(name).expanduser().resolve()
        if not target.exists():
            print(f"Error: Virtual environment '{name}' not found.")
            return False
    # Safety: ensure it's under venv_home or at least looks like a venv
    marker = target / ('bin' if os.name != 'nt' else 'Scripts') / ('activate' if os.name != 'nt' else 'activate.bat')
    if not marker.exists():
        print(f"Warning: {target} does not appear to be a virtual environment.")
        answer = input("Are you sure you want to delete it? (y/N): ")
        if answer.lower() != 'y':
            print("Aborted.")
            return False
    try:
        shutil.rmtree(target)
        print(f"Removed virtual environment {target}")
        return True
    except Exception as e:
        print(f"Failed to remove {target}: {e}")
        return False

def activate_venv(name):
    """Print the activation command for the virtual environment."""
    venv_home = get_venv_home()
    target = venv_home / name
    if not target.exists():
        target = Path(name).expanduser().resolve()
        if not target.exists():
            print(f"Error: Virtual environment '{name}' not found.")
            return False
    activate_script = target / ('bin' if os.name != 'nt' else 'Scripts') / ('activate' if os.name != 'nt' else 'activate.bat')
    if not activate_script.exists():
        print(f"Error: Cannot find activation script in {target}")
        return False
    if os.name == 'nt':
        cmd = f'"{activate_script}"'
    else:
        cmd = f"source {activate_script}"
    print(f"To activate the environment, run:")
    print(f"  {cmd}")
    return True

def detect_system_venv():
    """Detect system/global virtual environment from environment variables."""
    # Common vars
    for var in ('VIRTUAL_ENV', 'CONDA_DEFAULT_ENV', 'CONDA_PREFIX', 'WORKON_HOME', 'VENV_PATH'):
        val = os.environ.get(var)
        if val:
            print(f"{var}: {val}")
    # Also check if we are already in a venv
    if hasattr(sys, 'real_prefix') or (hasattr(sys, 'base_prefix') and sys.base_prefix != sys.prefix):
        print(f"Currently in virtual environment: {sys.prefix}")
    else:
        print("Not currently in a virtual environment.")

def main():
    parser = argparse.ArgumentParser(description="Manage Python virtual environments.")
    subparsers = parser.add_subparsers(dest='command', help='Available commands')

    # list
    subparsers.add_parser('list', help='List all virtual environments')

    # create
    create_parser = subparsers.add_parser('create', help='Create a new virtual environment')
    create_parser.add_argument('name', help='Name of the virtual environment')
    create_parser.add_argument('-p', '--path', help='Path where to create the venv (optional)')
    create_parser.add_argument('--python', help='Python interpreter to use (default: /Users/wws/.local/bin/python)')

    # remove
    remove_parser = subparsers.add_parser('remove', help='Remove a virtual environment')
    remove_parser.add_argument('name', help='Name or path of the virtual environment to remove')

    # activate
    activate_parser = subparsers.add_parser('activate', help='Show activation command for a virtual environment')
    activate_parser.add_argument('name', help='Name or path of the virtual environment to activate')

    # detect
    subparsers.add_parser('detect', help='Detect system/global environment variables related to venvs')

    args = parser.parse_args()

    if args.command == 'list':
        list_venvs()
    elif args.command == 'create':
        create_venv(args.name, args.path, args.python)
    elif args.command == 'remove':
        remove_venv(args.name)
    elif args.command == 'activate':
        activate_venv(args.name)
    elif args.command == 'detect':
        detect_system_venv()
    else:
        parser.print_help()

if __name__ == '__main__':
    main()