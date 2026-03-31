$projectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$frontendDir = Join-Path $projectRoot "frontend-web"

Start-Process powershell -ArgumentList @(
  "-NoExit",
  "-Command",
  "Set-Location '$projectRoot'; python .\servidor_backend_fase2.py"
)

Start-Process powershell -ArgumentList @(
  "-NoExit",
  "-Command",
  "Set-Location '$frontendDir'; npm run dev"
)

Write-Output "Backend e frontend iniciados em terminais separados."
Write-Output "Backend: http://localhost:8100"
Write-Output "Frontend: http://localhost:5173"
