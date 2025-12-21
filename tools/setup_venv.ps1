# Setup script for Python virtual environment in tools folder (Windows PowerShell)

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$VenvDir = Join-Path $ScriptDir "venv"

Write-Host "Setting up Python virtual environment in tools/venv..." -ForegroundColor Green

# Create virtual environment
python -m venv $VenvDir

# Activate and install dependencies
Write-Host "Installing dependencies..." -ForegroundColor Green
& "$VenvDir\Scripts\Activate.ps1"
pip install --upgrade pip
pip install -r "$ScriptDir\requirements.txt"

Write-Host ""
Write-Host "Virtual environment created successfully!" -ForegroundColor Green
Write-Host ""
Write-Host "To activate the virtual environment, run:" -ForegroundColor Yellow
Write-Host "  tools\venv\Scripts\Activate.ps1" -ForegroundColor Cyan
Write-Host ""
Write-Host "To deactivate, run:" -ForegroundColor Yellow
Write-Host "  deactivate" -ForegroundColor Cyan


