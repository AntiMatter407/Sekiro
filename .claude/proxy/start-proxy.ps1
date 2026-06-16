$Root = 'D:\Sekiro'
$Pythonw = 'C:\Users\yanping.guo\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\pythonw.exe'
$Python = 'C:\Users\yanping.guo\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe'
$ProxyScript = Join-Path $Root '.claude\proxy\proxy.py'
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

$Launcher = if (Test-Path $Pythonw) { $Pythonw } else { $Python }
if (-not (Test-Path $Launcher)) {
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

Write-Host "ERROR: Proxy failed to start. Check D:\Sekiro\.claude\proxy\proxy.log"
exit 1
