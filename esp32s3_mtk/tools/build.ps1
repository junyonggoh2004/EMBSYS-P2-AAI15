param(
    [ValidateSet('build','configure','flash','monitor','audit','sensors','ml')][string]$Action='build',
    [string]$Port,
    [ValidateSet('sine-float','sine-int8','autoencoder')][string]$Model='sine-float',
    [ValidateSet('all','aht-light')][string]$Inputs='all',
    [string]$BuildDirectory,
    [switch]$RawMonitor,
    [string]$ConfigFile
)
$ErrorActionPreference='Stop'
$MtkProjectRoot=(Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$MtkLocalConfig=Join-Path $PSScriptRoot 'local.env.ps1'
if (Test-Path -LiteralPath $MtkLocalConfig) { . $MtkLocalConfig }
if (-not $MtkKernelPath) { $MtkKernelPath=$env:MTKERNEL_PATH }
if (-not $MtkIdfPath) { $MtkIdfPath=$env:IDF_PATH }
if (-not $MtkToolsPath) { $MtkToolsPath=$env:IDF_TOOLS_PATH }
if (-not $MtkPythonPath) { $MtkPythonPath=$env:IDF_PYTHON_ENV_PATH }
foreach ($MtkRequiredPath in @($MtkKernelPath,$MtkIdfPath,$MtkToolsPath,$MtkPythonPath)) {
    if (-not $MtkRequiredPath -or -not (Test-Path -LiteralPath $MtkRequiredPath)) {
        throw 'Configure tools/local.env.ps1 or MTKERNEL_PATH, IDF_PATH, IDF_TOOLS_PATH and IDF_PYTHON_ENV_PATH. See README.md.'
    }
}
$MtkLock=Get-Content -Raw -LiteralPath (Join-Path $MtkProjectRoot 'dependencies.lock.json') | ConvertFrom-Json
$MtkActualKernel=(& git -C $MtkKernelPath rev-parse HEAD).Trim()
$MtkActualIdf=(& git -C $MtkIdfPath rev-parse HEAD).Trim()
if ($MtkActualKernel -ne $MtkLock.kernel.commit -or $MtkActualIdf -ne $MtkLock.esp_idf.commit) {
    throw 'Dependency revision differs from dependencies.lock.json. Review and update the lock explicitly before building.'
}
$MtkIdfCmakePath=$MtkIdfPath.Replace('\','/')
$MtkKernelCmakePath=$MtkKernelPath.Replace('\','/')
$env:IDF_PATH=$MtkIdfCmakePath
$env:IDF_TOOLS_PATH=$MtkToolsPath.Replace('\','/')
$env:IDF_PYTHON_ENV_PATH=$MtkPythonPath
$env:IDF_TARGET='esp32s3'
$env:ESP_IDF_VERSION='6.1'
$MtkCompilerBin=Join-Path $MtkToolsPath 'tools\xtensa-esp-elf\esp-15.2.0_20251204\xtensa-esp-elf\bin'
$MtkUlpBin=Join-Path $MtkToolsPath 'tools\esp32ulp-elf\2.38_20240113\esp32ulp-elf\bin'
$env:PATH=(@($MtkCompilerBin,$MtkUlpBin,$MtkCmakeBin,$MtkNinjaBin,(Join-Path $MtkPythonPath 'Scripts')) -join ';')+';'+$env:PATH
$MtkPython=Join-Path $MtkPythonPath 'Scripts\python.exe'
$MtkBuild=Join-Path $MtkProjectRoot 'build'
if ($BuildDirectory) { $MtkBuild=[System.IO.Path]::GetFullPath($BuildDirectory) }
if ($Action -eq 'audit') {
    & $MtkPython (Join-Path $PSScriptRoot 'audit_configurable.py') $MtkProjectRoot $MtkBuild
    if ($LASTEXITCODE -ne 0) { throw 'Build audit failed.' }
    exit 0
}
if (($Action -in @('flash','monitor','sensors','ml')) -and -not $Port) { throw 'Specify -Port COMx explicitly.' }
if ($Action -eq 'ml') {
    $MlArgs = @('--port', $Port, '--model', $Model, '--inputs', $Inputs)
    if ($ConfigFile) { $MlArgs += @('--config', $ConfigFile) }
    & $MtkPython (Join-Path $PSScriptRoot 'load_ml.py') @MlArgs
    if ($LASTEXITCODE -ne 0) { throw 'ML configuration was not acknowledged. Check firmware and port.' }
    exit 0
}
if ($Action -eq 'sensors') {
    & $MtkPython (Join-Path $PSScriptRoot 'load_sensors.py') --port $Port
    if ($LASTEXITCODE -ne 0) { throw 'Sensor configuration was not acknowledged. Check the firmware and port.' }
    exit 0
}
if ($Action -eq 'monitor') {
    Write-Output "Opening serial monitor on $Port. Typed characters are shown locally; Ctrl+C exits."
    if ($RawMonitor) {
        & $MtkPython -m serial.tools.miniterm --eol CRLF --raw --echo --exit-char 3 $Port 115200
    } else {
        Write-Output "Times are board uptime. Use MTK: monitor raw for complete serial output."
        & $MtkPython (Join-Path $PSScriptRoot 'monitor.py') --eol CRLF --echo --exit-char 3 $Port 115200
    }
    if ($LASTEXITCODE -ne 0) { throw 'Serial monitor failed.' }
    exit 0
}
$MtkArgs=@('-C',$MtkProjectRoot,'-B',$MtkBuild,'-D',"MTKERNEL_PATH=$MtkKernelCmakePath")
if ($Port) { $MtkArgs+=@('-p',$Port) }
if ($Action -eq 'configure') { $MtkArgs+='reconfigure' } else { $MtkArgs+=$Action }
& $MtkPython (Join-Path $MtkIdfPath 'tools\idf.py') @MtkArgs
if ($LASTEXITCODE -ne 0) { throw "ESP-IDF $Action failed (exit $LASTEXITCODE)." }
if ($Action -eq 'build') {
    & $MtkPython (Join-Path $PSScriptRoot 'audit_configurable.py') $MtkProjectRoot $MtkBuild
    if ($LASTEXITCODE -ne 0) { throw 'Firmware linked, but the kernel audit failed.' }
}
