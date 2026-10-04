param(
    [string]$ObsPath
)

$ErrorActionPreference = 'Stop'
$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$programFiles64 = if ($env:ProgramW6432) { $env:ProgramW6432 } else { $env:ProgramFiles }
$installDirectory = Join-Path $programFiles64 'ASIO OBS Splitter'
$proxyDll = Join-Path $installDirectory 'ProxyAsio64.dll'

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

function Get-ObsPluginPaths {
    if ($ObsPath) {
        return @(Join-Path $ObsPath 'obs-plugins\64bit\obs-asio-splitter.dll')
    }

    $programFiles64 = if ($env:ProgramW6432) { $env:ProgramW6432 } else { $env:ProgramFiles }
    return @(
        (Join-Path $programFiles64 'obs-studio\obs-plugins\64bit\obs-asio-splitter.dll'),
        (Join-Path $env:ProgramFiles 'obs-studio\obs-plugins\64bit\obs-asio-splitter.dll')
    ) | Select-Object -Unique
}

Assert-Administrator

if (Get-Process obs64 -ErrorAction SilentlyContinue) {
    throw 'OBS is running. Close OBS before uninstalling the plugin.'
}

$regsvr32 = Join-Path ${env:windir} 'System32\regsvr32.exe'
$proxyRegistryPaths = @(
    'Registry::HKEY_LOCAL_MACHINE\SOFTWARE\ASIO\Proxy ASIO (OBS Splitter)',
    'Registry::HKEY_LOCAL_MACHINE\SOFTWARE\Classes\CLSID\{A5C8E531-9F22-4D9A-8C37-F79526C8D8E1}'
)
if (Test-Path $proxyDll) {
    $unregistration = Start-Process -FilePath $regsvr32 -ArgumentList "/u /s `"$proxyDll`"" -Wait -PassThru
    if ($unregistration.ExitCode -ne 0) {
        throw "Proxy ASIO unregistration failed with exit code $($unregistration.ExitCode)."
    }
}

# regsvr32는 DLL이 삭제 실패를 보고하지 않아도 성공 코드를 반환할 수 있다.
# 따라서 이 프록시가 소유한 두 키만 명시적으로 확인하고 정리한다.
foreach ($registryPath in $proxyRegistryPaths) {
    if (Test-Path $registryPath) {
        Remove-Item -LiteralPath $registryPath -Recurse -Force
    }
    if (Test-Path $registryPath) {
        throw "Proxy registry key could not be removed: $registryPath"
    }
}

foreach ($pluginPath in (Get-ObsPluginPaths)) {
    if (Test-Path $pluginPath) {
        Remove-Item -LiteralPath $pluginPath -Force
        if (Test-Path $pluginPath) {
            throw "OBS plugin could not be removed: $pluginPath"
        }
        Write-Host "Removed OBS plugin: $pluginPath" -ForegroundColor Green
    }
}

Get-Process AsioSplitterTray -ErrorAction SilentlyContinue | Stop-Process -Force
$legacyStartupShortcut = Join-Path ([Environment]::GetFolderPath('Startup')) 'ASIO OBS Splitter.lnk'
if (Test-Path $legacyStartupShortcut) {
    Remove-Item -LiteralPath $legacyStartupShortcut -Force
}
$installDirectory = Join-Path $programFiles64 'ASIO OBS Splitter'
if (Test-Path $installDirectory) {
    Remove-Item -LiteralPath $installDirectory -Recurse -Force
}

Write-Host 'Proxy ASIO driver unregistered and OBS plugin removed.' -ForegroundColor Green
