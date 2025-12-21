#!/bin/bash
# Setup script for Python virtual environment in tools folder

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
VENV_DIR="$SCRIPT_DIR/venv"

echo "Setting up Python virtual environment in tools/venv..."

# Create virtual environment
python3 -m venv "$VENV_DIR"

# Activate and install dependencies
echo "Installing dependencies..."
source "$VENV_DIR/bin/activate"
pip install --upgrade pip
pip install -r "$SCRIPT_DIR/requirements.txt"

echo ""
echo "Virtual environment created successfully!"
echo ""
echo "To activate the virtual environment, run:"
echo "  source tools/venv/bin/activate"
echo ""
echo "To deactivate, run:"
echo "  deactivate"


