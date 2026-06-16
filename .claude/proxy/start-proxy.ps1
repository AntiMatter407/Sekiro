$Root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)

# Locate python: prefer system PATH lookup, then probe known cache directories
function Test-PythonWorks($exePath) {
    try {
        $result = & $exePath --version 2>&1
        return ($LASTEXITCODE -eq 0)
    } catch {
        return $false
    }
}

$Python = $null
# 1) Try python3 / python from PATH (but validate — WindowsApps stub fails silently)
foreach ($cmdName in @('python3', 'python')) {
    $candidate = (Get-Command $cmdName -ErrorAction SilentlyContinue).Source
    if ($candidate -and (Test-PythonWorks $candidate)) {
        $Python = $candidate
        break
    }
}
# 2) Fall back to common Windows Python install directories (machine-agnostic)
if (-not $Python) {
    $SearchDirs = @(
        "$env:LOCALAPPDATA\Programs\Python"
        "$env:USERPROFILE\.cache\codex-runtimes\codex-primary-runtime\dependencies\python"
    )
    # Also cover C:\Python3* and C:\Program Files\Python3* (Python.org installer defaults)
    foreach ($root in @("C:\", "C:\Program Files")) {
        foreach ($d in (Get-ChildItem $root -Directory -Filter "Python3*" -ErrorAction SilentlyContinue | Sort-Object Name -Descending)) {
            $SearchDirs += $d.FullName
        }
    }
    foreach ($dir in $SearchDirs) {
        foreach ($exe in @('python.exe', 'pythonw.exe', 'python3.exe')) {
            $candidate = Join-Path $dir $exe
            if ((Test-Path $candidate) -and (Test-PythonWorks $candidate)) {
                $Python = $candidate
                break
            }
        }
        if ($Python) { break }
    }
}

$ProxyScript = Join-Path $PSScriptRoot 'proxy.py'
$Port = 4000
$HealthUrl = "http://127.0.0.1:$Port/health"

function Test-ProxyHealth {
    try {
        $r = Invoke-WebRequest -Uri $HealthUrl -UseBasicParsing -TimeoutSec 2
        return ($r.StatusCode -eq 200)
    } catch {
        return $false
    }
}

if (Test-ProxyHealth) {
    Write-Host "Proxy already running: $HealthUrl"
    exit 0
}

$listeners = Get-NetTCPConnection -LocalPort $Port -State Listen -ErrorAction SilentlyContinue
$pids = $listeners.OwningProcess | Sort-Object -Unique
foreach ($pidToStop in $pids) {
    if ($pidToStop -and $pidToStop -ne $PID) {
        Stop-Process -Id $pidToStop -Force -ErrorAction SilentlyContinue
    }
}

for ($i = 0; $i -lt 10; $i++) {
    $still = Get-NetTCPConnection -LocalPort $Port -State Listen -ErrorAction SilentlyContinue
    if (-not $still) { break }
    Start-Sleep -Milliseconds 300
}

$Launcher = $Python
if (-not $Launcher) {
    Write-Host "ERROR: Python launcher not found: $Launcher"
    exit 1
}

Start-Process `
    -FilePath $Launcher `
    -ArgumentList @($ProxyScript) `
    -WorkingDirectory $Root `
    -WindowStyle Hidden

for ($i = 0; $i -lt 20; $i++) {
    Start-Sleep -Milliseconds 500
    if (Test-ProxyHealth) {
        Write-Host "Proxy started OK: $HealthUrl"
        exit 0
    }
}

Write-Host "ERROR: Proxy failed to start. Check $PSScriptRoot\proxy.log"
exit 1
