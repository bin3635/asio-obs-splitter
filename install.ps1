param(
    [string]$ObsPath
)

$ErrorActionPreference = 'Stop'
$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
function Find-BinaryFile([string]$fileName, [string]$buildSubPath) {
    $directPath = Join-Path $scriptRoot $fileName
    if (Test-Path $directPath) { return $directPath }
    $buildPath = Join-Path $scriptRoot $buildSubPath
    if (Test-Path $buildPath) { return $buildPath }
    return $directPath
}

$proxyDll = Find-BinaryFile 'ProxyAsio64.dll' 'build\proxy-asio\Release\ProxyAsio64.dll'
$pluginDll = Find-BinaryFile 'obs-asio-splitter.dll' 'build\obs-plugin\Release\obs-asio-splitter.dll'
$trayExe = Find-BinaryFile 'AsioSplitterTray.exe' 'build\tray\Release\AsioSplitterTray.exe'
$programFiles64 = if ($env:ProgramW6432) { $env:ProgramW6432 } else { $env:ProgramFiles }

function Assert-Administrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = [Security.Principal.WindowsPrincipal]::new($identity)
    if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        $arguments = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $PSCommandPath)
        if ($ObsPath) {
            $arguments += @('-ObsPath', $ObsPath)
        }
        $elevated = Start-Process powershell.exe -Verb RunAs -ArgumentList $arguments -Wait -PassThru
        exit $elevated.ExitCode
    }
}

function Find-ObsPluginDirectory {
    if ($ObsPath) {
        $customDirectory = Join-Path ([IO.Path]::GetFullPath($ObsPath)) 'obs-plugins\64bit'
        if (-not (Test-Path $customDirectory)) {
            throw "OBS 64-bit plugin directory was not found: $customDirectory"
        }
        return $customDirectory
    }

    $defaultDirectory = 'C:\Program Files\obs-studio\obs-plugins\64bit'
    if (-not (Test-Path $defaultDirectory)) {
        throw 'OBS 64-bit plugin directory was not found. Use -ObsPath to specify it.'
    }

    return $defaultDirectory
}

Assert-Administrator

if (-not (Test-Path $proxyDll)) {
    throw "Proxy DLL not found: $proxyDll. Build Release first."
}
if (-not (Test-Path $pluginDll)) {
    throw "OBS plugin DLL not found: $pluginDll. Build Release first."
}
if (-not (Test-Path $trayExe)) {
    throw "Tray application not found: $trayExe. Build Release first."
}

$obsPluginDirectory = Find-ObsPluginDirectory
$obsPluginPath = Join-Path $obsPluginDirectory 'obs-asio-splitter.dll'
$regsvr32 = Join-Path ${env:windir} 'System32\regsvr32.exe'

if (Get-Process obs64 -ErrorAction SilentlyContinue) {
    throw 'OBS is running. Close OBS before installing the plugin.'
}

$registration = Start-Process -FilePath $regsvr32 -ArgumentList @('/s', $proxyDll) -Wait -PassThru
if ($registration.ExitCode -ne 0) {
    throw "Proxy ASIO registration failed with exit code $($registration.ExitCode)."
}

Write-Host "Installing OBS plugin to: $obsPluginPath"
Copy-Item $pluginDll $obsPluginPath -Force
if (-not (Test-Path $obsPluginPath)) {
    throw "OBS plugin was not copied to: $obsPluginPath"
}

$installDirectory = Join-Path $programFiles64 'ASIO OBS Splitter'
$installedTrayExe = Join-Path $installDirectory 'AsioSplitterTray.exe'
$legacyStartupShortcut = Join-Path ([Environment]::GetFolderPath('Startup')) 'ASIO OBS Splitter.lnk'
if (Test-Path $legacyStartupShortcut) {
    Remove-Item -LiteralPath $legacyStartupShortcut -Force
}
New-Item -ItemType Directory -Path $installDirectory -Force | Out-Null
Copy-Item $trayExe $installedTrayExe -Force

Write-Host 'Proxy ASIO driver registered.' -ForegroundColor Green
Write-Host "OBS plugin installed to: $obsPluginPath" -ForegroundColor Green
Write-Host "Tray selector installed to: $installedTrayExe" -ForegroundColor Green
Write-Host 'Restart OBS and the ASIO host before testing.'
