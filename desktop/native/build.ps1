Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$Output = Join-Path $Root "out"
New-Item -ItemType Directory -Force -Path $Output | Out-Null
$Compiler = (Get-Command cl.exe -ErrorAction Stop).Source

& $Compiler /nologo /std:c++20 /O2 /MT /EHsc /W4 /WX /guard:cf `
  /DUNICODE /D_UNICODE (Join-Path $Root "codex-input-helper.cpp") `
  /Fe:(Join-Path $Output "codex-input-helper.exe") `
  /link /DYNAMICBASE /HIGHENTROPYVA /NXCOMPAT user32.lib shell32.lib
if ($LASTEXITCODE -ne 0) { throw "Input helper build failed" }

& $Compiler /nologo /std:c++20 /O2 /MT /EHsc /W4 /WX /guard:cf `
  /DUNICODE /D_UNICODE (Join-Path $Root "codex-ble-helper.cpp") `
  /Fe:(Join-Path $Output "codex-ble-helper.exe") `
  /link /DYNAMICBASE /HIGHENTROPYVA /NXCOMPAT windowsapp.lib crypt32.lib
if ($LASTEXITCODE -ne 0) { throw "BLE helper build failed" }

& $Compiler /nologo /std:c++20 /O2 /MT /EHsc /W4 /WX /guard:cf `
  /DUNICODE /D_UNICODE (Join-Path $Root "codex-input-detector.cpp") `
  /Fe:(Join-Path $Output "codex-input-detector.exe") `
  /link /DYNAMICBASE /HIGHENTROPYVA /NXCOMPAT user32.lib
if ($LASTEXITCODE -ne 0) { throw "Input detector build failed" }

& (Join-Path $Output "codex-input-detector.exe") `
  (Join-Path $Output "codex-input-helper.exe")
if ($LASTEXITCODE -ne 0) { throw "Input helper integration test failed" }
