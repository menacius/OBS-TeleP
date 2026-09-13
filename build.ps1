param(
  [ValidateSet("x64")]
  [string]$Architecture = "x64",

  [ValidateSet("Debug", "Release", "RelWithDebInfo", "MinSizeRel")]
  [string]$Configuration = "RelWithDebInfo",

  [string]$BuildDir = "build",
  [string]$DepsDir = ".deps",
  [string]$GradleVersion = "8.7",
  [string]$AndroidCompileSdk = "35",
  [string]$AndroidBuildTools = "35.0.0",
  [switch]$Clean,
  [switch]$BuildAndroid
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$BuildPath = Join-Path $Root $BuildDir
if ([System.IO.Path]::IsPathRooted($DepsDir)) {
  $DepsPath = $DepsDir
} else {
  $DepsPath = Join-Path $Root $DepsDir
}
$ObsStudioPath = Join-Path (Split-Path $Root -Parent) "obs-studio"
$PresetPath = Join-Path $ObsStudioPath "CMakePresets.json"

function Write-Step([string]$Message) {
  Write-Host "==> $Message" -ForegroundColor Cyan
}

function Assert-Command([string]$Name, [string]$InstallHint) {
  if (-not (Get-Command $Name -ErrorAction SilentlyContinue)) {
    throw "Missing required tool '$Name'. $InstallHint"
  }
}

function Get-DependencySpec {
  $fallback = [pscustomobject]@{
    Version = "2025-08-23"
    BaseUrl = "https://github.com/obsproject/obs-deps/releases/download"
    PrebuiltHash = $null
    QtHash = $null
  }

  if (-not (Test-Path -LiteralPath $PresetPath)) {
    return $fallback
  }

  $json = Get-Content -LiteralPath $PresetPath -Raw | ConvertFrom-Json
  foreach ($preset in $json.configurePresets) {
    if (-not $preset.PSObject.Properties.Match("vendor").Count) {
      continue
    }
    $vendorRoot = $preset.vendor
    if (-not $vendorRoot.PSObject.Properties.Match("obsproject.com/obs-studio").Count) {
      continue
    }
    $vendor = $vendorRoot.'obsproject.com/obs-studio'
    if ($null -eq $vendor -or $null -eq $vendor.dependencies) {
      continue
    }

    $deps = $vendor.dependencies
    return [pscustomobject]@{
      Version = [string]$deps.prebuilt.version
      BaseUrl = [string]$deps.prebuilt.baseUrl
      PrebuiltHash = [string]$deps.prebuilt.hashes.'windows-x64'
      QtHash = [string]$deps.qt6.hashes.'windows-x64'
    }
  }

  return $fallback
}

function Test-ObsSdkComplete([string]$Path) {
  return (Test-Path -LiteralPath (Join-Path $Path "include\obs.h")) -and
         (Test-Path -LiteralPath (Join-Path $Path "lib\obs.lib")) -and
         (Test-Path -LiteralPath (Join-Path $Path "lib\obs-frontend-api.lib"))
}

function Test-QtComplete([string]$Path) {
  return Test-Path -LiteralPath (Join-Path $Path "lib\cmake\Qt6\Qt6Config.cmake")
}

function Find-ExistingObsSdk([string]$Version) {
  $roots = @(
    $env:OBS_SDK_DIR,
    $DepsPath,
    (Join-Path (Split-Path $Root -Parent) "obs-build-dependencies"),
    (Join-Path $env:USERPROFILE "Desktop\obs-build-dependencies")
  )

  foreach ($rootPath in $roots) {
    if ([string]::IsNullOrWhiteSpace($rootPath)) {
      continue
    }
    if (-not (Test-Path -LiteralPath $rootPath)) {
      continue
    }

    $preferred = Join-Path $rootPath "plugin-deps-$Version-qt6-$Architecture"
    if (Test-ObsSdkComplete $preferred) {
      return $preferred
    }

    $candidates = Get-ChildItem -LiteralPath $rootPath -Directory -Filter "plugin-deps-*-qt6-$Architecture" -ErrorAction SilentlyContinue
    foreach ($candidate in $candidates) {
      if (Test-ObsSdkComplete $candidate.FullName) {
        return $candidate.FullName
      }
    }
  }

  return $null
}

function New-ObsSdkFromSourceBuild([string]$Destination) {
  $libobsLib = Join-Path $ObsStudioPath "plugin_build_x64\libobs\$Configuration\obs.lib"
  $frontendLib = Join-Path $ObsStudioPath "plugin_build_x64\frontend\api\$Configuration\obs-frontend-api.lib"
  $libobsDll = Join-Path $ObsStudioPath "plugin_build_x64\libobs\$Configuration\obs.dll"
  $frontendDll = Join-Path $ObsStudioPath "plugin_build_x64\frontend\api\$Configuration\obs-frontend-api.dll"

  if (-not ((Test-Path -LiteralPath $libobsLib) -and (Test-Path -LiteralPath $frontendLib))) {
    return $false
  }

  if (Test-Path -LiteralPath $Destination) {
    Remove-Item -LiteralPath $Destination -Recurse -Force
  }
  New-Item -ItemType Directory -Force -Path (Join-Path $Destination "include") | Out-Null
  New-Item -ItemType Directory -Force -Path (Join-Path $Destination "lib") | Out-Null
  New-Item -ItemType Directory -Force -Path (Join-Path $Destination "bin\64bit") | Out-Null

  Copy-Item -Path (Join-Path $ObsStudioPath "libobs\*.h") -Destination (Join-Path $Destination "include")
  Copy-Item -LiteralPath (Join-Path $ObsStudioPath "libobs\util") -Destination (Join-Path $Destination "include\util") -Recurse
  Copy-Item -LiteralPath (Join-Path $ObsStudioPath "libobs\graphics") -Destination (Join-Path $Destination "include\graphics") -Recurse
  Copy-Item -LiteralPath (Join-Path $ObsStudioPath "frontend\api\obs-frontend-api.h") -Destination (Join-Path $Destination "include")
  Copy-Item -LiteralPath $libobsLib -Destination (Join-Path $Destination "lib\obs.lib")
  Copy-Item -LiteralPath $frontendLib -Destination (Join-Path $Destination "lib\obs-frontend-api.lib")

  if (Test-Path -LiteralPath $libobsDll) {
    Copy-Item -LiteralPath $libobsDll -Destination (Join-Path $Destination "bin\64bit\obs.dll")
  }
  if (Test-Path -LiteralPath $frontendDll) {
    Copy-Item -LiteralPath $frontendDll -Destination (Join-Path $Destination "bin\64bit\obs-frontend-api.dll")
  }

  return Test-ObsSdkComplete $Destination
}

function Invoke-Download([string]$Url, [string]$Destination) {
  if (Test-Path -LiteralPath $Destination) {
    return
  }

  Write-Step "Downloading $Url"
  try {
    Invoke-WebRequest -Uri $Url -OutFile $Destination -UseBasicParsing
  } catch {
    if (Get-Command curl.exe -ErrorAction SilentlyContinue) {
      & curl.exe -L --fail --output $Destination $Url
      if ($LASTEXITCODE -ne 0) {
        throw "curl failed with exit code $LASTEXITCODE while downloading $Url"
      }
    } else {
      throw
    }
  }
}

function Assert-Hash([string]$Path, [string]$Expected) {
  if ([string]::IsNullOrWhiteSpace($Expected)) {
    Write-Host "Skipping hash check for $(Split-Path $Path -Leaf): no hash metadata available."
    return
  }

  $actual = (Get-FileHash -Algorithm SHA256 -LiteralPath $Path).Hash.ToLowerInvariant()
  if ($actual -ne $Expected.ToLowerInvariant()) {
    throw "SHA-256 mismatch for '$Path'. Expected $Expected, got $actual."
  }
}

function Expand-ZipInto([string]$ZipPath, [string]$Destination) {
  Write-Step "Extracting $(Split-Path $ZipPath -Leaf)"
  Expand-Archive -LiteralPath $ZipPath -DestinationPath $Destination -Force
}

function Find-FirstFile([string]$RootPath, [string]$Filter) {
  if (-not (Test-Path -LiteralPath $RootPath)) {
    return $null
  }
  $match = Get-ChildItem -LiteralPath $RootPath -Recurse -File -Filter $Filter -ErrorAction SilentlyContinue | Select-Object -First 1
  if ($match) {
    return $match.FullName
  }
  return $null
}

function Ensure-Jdk {
  $java = Get-Command java -ErrorAction SilentlyContinue
  if ($java) {
    return $java.Source
  }

  if ($env:JAVA_HOME) {
    $javaFromHome = Join-Path $env:JAVA_HOME "bin\java.exe"
    if (Test-Path -LiteralPath $javaFromHome) {
      return $javaFromHome
    }
  }

  $jdkRoot = Join-Path $DepsPath "jdk"
  $existingJava = Find-FirstFile $jdkRoot "java.exe"
  if ($existingJava) {
    $env:JAVA_HOME = Split-Path (Split-Path $existingJava -Parent) -Parent
    $env:Path = "$(Split-Path $existingJava -Parent);$env:Path"
    return $existingJava
  }

  New-Item -ItemType Directory -Force -Path $jdkRoot | Out-Null
  $jdkZip = Join-Path $jdkRoot "temurin-jdk-17.zip"
  Invoke-Download "https://api.adoptium.net/v3/binary/latest/17/ga/windows/x64/jdk/hotspot/normal/eclipse?project=jdk" $jdkZip
  Expand-ZipInto $jdkZip $jdkRoot

  $downloadedJava = Find-FirstFile $jdkRoot "java.exe"
  if (-not $downloadedJava) {
    throw "JDK download did not produce java.exe under '$jdkRoot'."
  }

  $env:JAVA_HOME = Split-Path (Split-Path $downloadedJava -Parent) -Parent
  $env:Path = "$(Split-Path $downloadedJava -Parent);$env:Path"
  return $downloadedJava
}

function Get-AndroidSdkPath {
  if ($env:ANDROID_HOME -and (Test-Path -LiteralPath $env:ANDROID_HOME)) {
    return $env:ANDROID_HOME
  }
  if ($env:ANDROID_SDK_ROOT -and (Test-Path -LiteralPath $env:ANDROID_SDK_ROOT)) {
    return $env:ANDROID_SDK_ROOT
  }

  $defaultSdk = Join-Path $env:LOCALAPPDATA "Android\Sdk"
  if (Test-Path -LiteralPath $defaultSdk) {
    return $defaultSdk
  }

  return Join-Path $DepsPath "android-sdk"
}

function Ensure-AndroidSdk {
  $sdkRoot = Get-AndroidSdkPath
  New-Item -ItemType Directory -Force -Path $sdkRoot | Out-Null

  $env:ANDROID_HOME = $sdkRoot
  $env:ANDROID_SDK_ROOT = $sdkRoot

  $sdkManager = Join-Path $sdkRoot "cmdline-tools\latest\bin\sdkmanager.bat"
  if (-not (Test-Path -LiteralPath $sdkManager)) {
    $toolsRoot = Join-Path $sdkRoot "cmdline-tools"
    $tempTools = Join-Path $toolsRoot "cmdline-tools"
    $latestTools = Join-Path $toolsRoot "latest"
    New-Item -ItemType Directory -Force -Path $toolsRoot | Out-Null

    $toolsZip = Join-Path $DepsPath "commandlinetools-win-latest.zip"
    Invoke-Download "https://dl.google.com/android/repository/commandlinetools-win-11076708_latest.zip" $toolsZip
    Expand-ZipInto $toolsZip $toolsRoot

    if (Test-Path -LiteralPath $latestTools) {
      Remove-Item -LiteralPath $latestTools -Recurse -Force
    }
    if (-not (Test-Path -LiteralPath $tempTools)) {
      throw "Android command-line tools download did not produce '$tempTools'."
    }
    Move-Item -LiteralPath $tempTools -Destination $latestTools
  }

  if (-not (Test-Path -LiteralPath $sdkManager)) {
    throw "Android SDK manager was not found at '$sdkManager'."
  }

  $acceptLicenses = {
    $answers = ((1..200 | ForEach-Object { "y" }) -join [Environment]::NewLine) + [Environment]::NewLine
    $answers | & $sdkManager --sdk_root="$sdkRoot" --licenses | ForEach-Object { Write-Host $_ }
    if ($LASTEXITCODE -ne 0) {
      throw "Android SDK license acceptance failed with exit code $LASTEXITCODE"
    }
  }

  & $acceptLicenses

  Write-Step "Installing Android SDK packages"
  $sdkPackages = @(
    "platform-tools",
    "platforms;android-$AndroidCompileSdk",
    "build-tools;$AndroidBuildTools",
    "build-tools;34.0.0"
  )
  & $sdkManager --sdk_root="$sdkRoot" $sdkPackages | ForEach-Object { Write-Host $_ }
  if ($LASTEXITCODE -ne 0) {
    throw "sdkmanager package install failed with exit code $LASTEXITCODE"
  }

  & $acceptLicenses

  return $sdkRoot
}

function Ensure-QtDeps {
  New-Item -ItemType Directory -Force -Path $DepsPath | Out-Null

  $spec = Get-DependencySpec
  $qtDirName = "obs-deps-qt6-$($spec.Version)-$Architecture"
  $qtPath = Join-Path $DepsPath $qtDirName

  if (Test-QtComplete $qtPath) {
    Write-Step "Using existing Qt deps: $qtPath"
    return $qtPath
  }

  $qtZip = Join-Path $DepsPath "windows-deps-qt6-$($spec.Version)-$Architecture.zip"
  $qtUrl = "$($spec.BaseUrl)/$($spec.Version)/$(Split-Path $qtZip -Leaf)"

  Invoke-Download $qtUrl $qtZip
  Assert-Hash $qtZip $spec.QtHash

  $tempPath = Join-Path $DepsPath "$qtDirName.tmp"
  if (Test-Path -LiteralPath $tempPath) {
    Remove-Item -LiteralPath $tempPath -Recurse -Force
  }
  New-Item -ItemType Directory -Force -Path $tempPath | Out-Null

  Expand-ZipInto $qtZip $tempPath

  if (-not (Test-QtComplete $tempPath)) {
    throw "Downloaded Qt dependency archive did not produce a usable Qt tree in '$tempPath'."
  }

  if (Test-Path -LiteralPath $qtPath) {
    Remove-Item -LiteralPath $qtPath -Recurse -Force
  }
  Move-Item -LiteralPath $tempPath -Destination $qtPath
  Write-Step "Prepared Qt deps: $qtPath"
  return $qtPath
}

function Ensure-ObsPluginSdk {
  New-Item -ItemType Directory -Force -Path $DepsPath | Out-Null

  $spec = Get-DependencySpec
  $existing = Find-ExistingObsSdk $spec.Version
  if ($existing) {
    Write-Step "Using existing OBS plugin SDK: $existing"
    return $existing
  }

  $generatedSdk = Join-Path $DepsPath "obs-sdk-from-source-$Architecture"
  if (New-ObsSdkFromSourceBuild $generatedSdk) {
    Write-Step "Prepared OBS SDK from adjacent obs-studio build: $generatedSdk"
    return $generatedSdk
  }

  throw "Could not find OBS headers/import libraries. Downloading OBS third-party deps is not enough for plugins. Build the adjacent obs-studio checkout first, or provide a plugin SDK with include\obs.h, lib\obs.lib, and lib\obs-frontend-api.lib via -DepsDir or OBS_SDK_DIR."
}

function Build-Native {
  Assert-Command cmake "Install CMake and make sure it is on PATH."

  $sdkPath = Ensure-ObsPluginSdk
  $qtPath = Ensure-QtDeps
  $qtDir = Join-Path $qtPath "lib\cmake\Qt6"

  if ($Clean -and (Test-Path -LiteralPath $BuildPath)) {
    Write-Step "Cleaning $BuildPath"
    Remove-Item -LiteralPath $BuildPath -Recurse -Force
  }

  Write-Step "Configuring native plugin"
  $cmakeArgs = @("-S", $Root, "-B", $BuildPath, "-DOBS_SDK_DIR=$sdkPath", "-DQt6_DIR=$qtDir")
  if (-not (Test-Path -LiteralPath (Join-Path $BuildPath "CMakeCache.txt"))) {
    $cmakeArgs += @("-A", $Architecture)
  }
  & cmake @cmakeArgs
  if ($LASTEXITCODE -ne 0) {
    throw "CMake configure failed with exit code $LASTEXITCODE"
  }

  Write-Step "Building native plugin ($Configuration)"
  & cmake --build $BuildPath --config $Configuration
  if ($LASTEXITCODE -ne 0) {
    throw "Native build failed with exit code $LASTEXITCODE"
  }

  $dll = Join-Path $BuildPath "o-prompter\bin\64bit\o-prompter.dll"
  if (-not (Test-Path -LiteralPath $dll)) {
    throw "Build completed but expected plugin DLL was not found at '$dll'."
  }
  Write-Host "Native plugin: $dll" -ForegroundColor Green
}

function Build-AndroidApp {
  $androidDir = Join-Path $Root "android"
  if (-not (Test-Path -LiteralPath $androidDir)) {
    throw "Android project folder not found: $androidDir"
  }
  $javaCommand = Ensure-Jdk
  Write-Step "Using Java: $javaCommand"

  $androidSdk = Ensure-AndroidSdk
  Write-Step "Using Android SDK: $androidSdk"

  $gradle = Get-Command gradle -ErrorAction SilentlyContinue
  if ($gradle) {
    $gradleCommand = $gradle.Source
  } else {
    $gradleRoot = Join-Path $DepsPath "gradle"
    $gradleHome = Join-Path $gradleRoot "gradle-$GradleVersion"
    $gradleCommand = Join-Path $gradleHome "bin\gradle.bat"
    if (-not (Test-Path -LiteralPath $gradleCommand)) {
      New-Item -ItemType Directory -Force -Path $gradleRoot | Out-Null
      $gradleZip = Join-Path $gradleRoot "gradle-$GradleVersion-bin.zip"
      Invoke-Download "https://services.gradle.org/distributions/gradle-$GradleVersion-bin.zip" $gradleZip
      Expand-ZipInto $gradleZip $gradleRoot
    }
    if (-not (Test-Path -LiteralPath $gradleCommand)) {
      throw "Gradle download did not produce '$gradleCommand'."
    }
  }

  Write-Step "Building Android remote"
  Push-Location $androidDir
  try {
    & $gradleCommand assembleDebug
    if ($LASTEXITCODE -ne 0) {
      throw "Android build failed with exit code $LASTEXITCODE"
    }
  } finally {
    Pop-Location
  }
}

Build-Native

if ($BuildAndroid) {
  Build-AndroidApp
}
