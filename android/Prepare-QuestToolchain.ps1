# Builds the toolchain the Quest app carries so a player can build the game on the headset
# ("Build on this Quest"), without a PC:
#
#  * translator/  the WiiCompiled translator published for linux-bionic-arm64 (.NET 10, which runs
#                 it on Mono), started through translator_host (android/toolchain/translator_host.c);
#  * llvm/        Termux's Android build of clang and lld 21.1.8 with exactly the shared libraries
#                 they load, OpenSSL for the translator's hashing (Android only has BoringSSL), and
#                 clang's own builtin headers;
#  * ndk.json     the Android NDK files a build also needs (the aarch64 sysroot, libunwind, libatomic,
#                 compiler-rt builtins). The NDK is Google's to distribute, so the headset downloads
#                 them from dl.google.com with HTTP range requests (about 8 MB of the 784 MB zip) and
#                 checks each file against the SHA-256 recorded here;
#  * toolchain.json  every file of translator/ and llvm/ with its SHA-256, and the fingerprint over
#                 them. Those two directories travel packed in files.zip.
#
#   powershell -ExecutionPolicy Bypass -File android/Prepare-QuestToolchain.ps1 -OutputDir <dir> -NdkLlvm <ndk>\toolchains\llvm\prebuilt\windows-x86_64
#
# The Gradle build runs it (prepareQuestToolchain). Downloads are cached in android/.dependencies.
# Needs the .NET 10 SDK: the linux-bionic runtime packs only exist for .NET 10.
[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string]$OutputDir,
    [Parameter(Mandatory)] [string]$NdkLlvm,
    [string]$CacheDir = '',
    [string]$Dotnet = 'dotnet'
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
# Windows PowerShell leaves $PSScriptRoot empty while binding parameter defaults.
if (-not $CacheDir) { $CacheDir = Join-Path $PSScriptRoot '.dependencies\quest-toolchain' }

$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$termuxRoot = 'https://packages.termux.dev/apt/termux-main/'
$termuxPrefix = './data/data/com.termux/files/usr/'

# Termux packages (aarch64), pinned by the SHA-256 of their .deb. Files maps a path inside the
# package to its name in llvm/: the libraries are stored under the SONAME their users load.
$termuxPackages = @(
    @{ Name = 'clang'; File = 'pool/main/c/clang/clang_21.1.8-3_aarch64.deb'; Sha256 = '9fb565013b96ce10abd8f8f217aaebb48f72873f53acbf8886329bcea841a21c'
       License = 'Apache-2.0 WITH LLVM-exception'
       Files = @{ 'bin/clang-21' = 'bin/clang-21'; 'lib/libclang-cpp.so' = 'lib/libclang-cpp.so'; 'lib/clang/21/include/' = 'lib/clang/21/include/' } }
    @{ Name = 'lld'; File = 'pool/main/l/lld/lld_21.1.8-3_aarch64.deb'; Sha256 = '6299c8fdff59dcff972f6b444805bcdb211c2bd797a64d2ddb3875e699f30630'
       License = 'Apache-2.0 WITH LLVM-exception'
       Files = @{ 'bin/lld' = 'bin/ld.lld' } }
    @{ Name = 'libllvm'; File = 'pool/main/libl/libllvm/libllvm_21.1.8-3_aarch64.deb'; Sha256 = '5056a73f2645fc9c0759e6f94a1bd6edcf36305acaef58dc685211ced9cc2a02'
       License = 'Apache-2.0 WITH LLVM-exception'
       Files = @{ 'lib/libLLVM.so' = 'lib/libLLVM.so'; 'share/doc/libllvm/LICENSE.TXT' = 'licenses/llvm/LICENSE.TXT' } }
    @{ Name = 'libc++'; File = 'pool/main/libc/libc++/libc++_30_aarch64.deb'; Sha256 = '53d0b84a7ba7459024257cb94d5b136fe13ef858567f65a8064b35950799f2ca'
       License = 'Apache-2.0 WITH LLVM-exception'
       Files = @{ 'lib/libc++_shared.so' = 'lib/libc++_shared.so' } }
    @{ Name = 'libffi'; File = 'pool/main/libf/libffi/libffi_3.8.0_aarch64.deb'; Sha256 = '4f255badf74cd31f6a2801c17fa1444199c84c834b517b7c843e2fe9ebe91d77'
       License = 'MIT'
       Files = @{ 'lib/libffi.so' = 'lib/libffi.so'; 'share/doc/libffi/copyright' = 'licenses/libffi/copyright' } }
    @{ Name = 'libxml2'; File = 'pool/main/libx/libxml2/libxml2_2.15.4-1_aarch64.deb'; Sha256 = 'd315861bbcf716cfb088f53cddab9fb6a96430cc66769f79774cc69c2f53bcff'
       License = 'MIT'
       Files = @{ 'lib/libxml2.so.16.1.4' = 'lib/libxml2.so.16' } }
    @{ Name = 'libiconv'; File = 'pool/main/libi/libiconv/libiconv_1.19_aarch64.deb'; Sha256 = 'fe9481b1dc101c6c3552943f25435109fd522aecc615ae49594f9fbee863bb37'
       License = 'LGPL-2.1-or-later'
       Files = @{ 'lib/libiconv.so' = 'lib/libiconv.so' } }
    @{ Name = 'zlib'; File = 'pool/main/z/zlib/zlib_1.3.2_aarch64.deb'; Sha256 = '75e7d0af17fcc3b40004309fdc00a1ddb9ae08346dce5e269902c34ac3966ac9'
       License = 'Zlib'
       Files = @{ 'lib/libz.so.1.3.2' = 'lib/libz.so.1' } }
    @{ Name = 'zstd'; File = 'pool/main/z/zstd/zstd_1.5.7-1_aarch64.deb'; Sha256 = 'e1b4a5113648da8de189620ba1fce74c48b2d0833d9043391b9a1c91fb606fd3'
       License = 'BSD-3-Clause'
       Files = @{ 'lib/libzstd.so.1.5.7' = 'lib/libzstd.so.1' } }
    @{ Name = 'openssl'; File = 'pool/main/o/openssl/openssl_1:3.6.3_aarch64.deb'; Sha256 = '86760e9ce736f463236f2c15b1eb3a3fdcfc5778d0fd7077a917448dcc90f3aa'
       License = 'Apache-2.0'
       # .NET's OpenSSL shim on Android loads "libssl.so" by that name; libssl needs libcrypto.so.3.
       Files = @{ 'lib/libssl.so.3' = 'lib/libssl.so'; 'lib/libcrypto.so.3' = 'lib/libcrypto.so.3' } }
)

# The NDK files the headset downloads: the r29 Linux zip, pinned by size and by the digest of the
# selected files (Get-ListDigest over them). The Linux zip is used because Windows file systems
# cannot hold its sysroot headers whose names differ only in case.
$ndkUrl = 'https://dl.google.com/android/repository/android-ndk-r29-linux.zip'
$ndkSize = 783549481
$ndkPrefix = 'android-ndk-r29/toolchains/llvm/prebuilt/linux-x86_64/'
$ndkSubsetDigest = '4221ed656e864cb3434d922e8e9eb916b08833718380847d0923527394041d0f'

function Get-Sha256Hex([string]$Path) {
    $sha = [Security.Cryptography.SHA256]::Create()
    try {
        $stream = [IO.File]::OpenRead($Path)
        try { $bytes = $sha.ComputeHash($stream) } finally { $stream.Dispose() }
    } finally { $sha.Dispose() }
    return (($bytes | ForEach-Object { $_.ToString('x2') }) -join '')
}

function Get-BytesSha256Hex([byte[]]$Bytes) {
    $sha = [Security.Cryptography.SHA256]::Create()
    try { $hash = $sha.ComputeHash($Bytes) } finally { $sha.Dispose() }
    return (($hash | ForEach-Object { $_.ToString('x2') }) -join '')
}

# SHA-256 over "<path> <sha256>" lines sorted by path, joined with \n. The headset computes the
# same over the files it downloaded.
function Get-ListDigest([object[]]$Files) {
    [string[]]$lines = @($Files | ForEach-Object { "$($_.path) $($_.sha256)" })
    [Array]::Sort($lines, [StringComparer]::Ordinal)
    return Get-BytesSha256Hex ([Text.Encoding]::UTF8.GetBytes(($lines -join "`n")))
}

function Invoke-Tar([string[]]$Arguments) {
    & (Join-Path $env:SystemRoot 'System32\tar.exe') @Arguments
    if ($LASTEXITCODE -ne 0) { throw "tar $($Arguments -join ' ') failed ($LASTEXITCODE)" }
}

function Save-TermuxPackage($Package) {
    $directory = Join-Path $CacheDir 'termux'
    New-Item -ItemType Directory -Force $directory | Out-Null
    # Termux file names can contain ':', which Windows does not allow.
    $local = Join-Path $directory ($Package.File.Split('/')[-1].Replace(':', '_'))
    if (-not (Test-Path $local) -or (Get-Sha256Hex $local) -ne $Package.Sha256) {
        $url = $termuxRoot + $Package.File.Replace(':', '%3A')
        Write-Host "Downloading $url"
        Invoke-WebRequest -UseBasicParsing -Uri $url -OutFile "$local.partial"
        $actual = Get-Sha256Hex "$local.partial"
        if ($actual -ne $Package.Sha256) {
            Remove-Item "$local.partial"
            throw "$($Package.Name) package digest mismatch: $actual"
        }
        Move-Item -Force "$local.partial" $local
    }
    return $local
}

function Expand-TermuxPackage($Package, [string]$Destination, [string]$Work) {
    $deb = Save-TermuxPackage $Package
    $unpack = Join-Path $Work $Package.Name
    if (Test-Path $unpack) { Remove-Item -Recurse -Force $unpack }
    New-Item -ItemType Directory -Force $unpack | Out-Null
    Invoke-Tar @('-xf', $deb, '-C', $unpack)
    $data = Get-ChildItem $unpack -Filter 'data.tar.*' | Select-Object -First 1
    if (-not $data) { throw "$($Package.Name) has no data archive" }
    # Only the listed regular files: the packages also hold symlinks, which Windows cannot create.
    $members = @($Package.Files.Keys | ForEach-Object { $termuxPrefix + $_ })
    Invoke-Tar (@('-xf', $data.FullName, '-C', $unpack) + $members)
    foreach ($source in $Package.Files.Keys) {
        $from = Join-Path $unpack ($termuxPrefix.Substring(2) + $source)
        $to = Join-Path $Destination $Package.Files[$source]
        if ($source.EndsWith('/')) {
            New-Item -ItemType Directory -Force $to | Out-Null
            Copy-Item -Recurse -Force (Join-Path $from '*') $to
        } else {
            if (-not (Test-Path $from -PathType Leaf)) { throw "$($Package.Name) has no regular file $source" }
            New-Item -ItemType Directory -Force (Split-Path -Parent $to) | Out-Null
            Copy-Item -Force $from $to
        }
    }
    Remove-Item -Recurse -Force $unpack
}

function Read-RemoteRange([string]$Url, [long]$Start, [long]$End) {
    $request = [Net.HttpWebRequest]::Create($Url)
    $request.AddRange($Start, $End)
    $response = $request.GetResponse()
    try {
        if ([int]$response.StatusCode -ne 206) { throw "$Url ignored the range request" }
        $stream = $response.GetResponseStream()
        $buffer = New-Object byte[] ([int]($End - $Start + 1))
        $read = 0
        while ($read -lt $buffer.Length) {
            $count = $stream.Read($buffer, $read, $buffer.Length - $read)
            if ($count -le 0) { throw "Short read from $Url" }
            $read += $count
        }
        return , $buffer
    } finally { $response.Close() }
}

# Whether a file below the NDK's prebuilt directory belongs to what the headset downloads.
function Test-NdkSubsetFile([string]$Path) {
    if ($Path.StartsWith('sysroot/usr/include/')) {
        return $Path -notmatch '^sysroot/usr/include/(arm-linux-androideabi|i686-linux-android|x86_64-linux-android|riscv64-linux-android)/'
    }
    if ($Path.StartsWith('sysroot/usr/lib/aarch64-linux-android/29/')) { return $true }
    if ($Path -in 'sysroot/usr/lib/aarch64-linux-android/libc++_shared.so', 'sysroot/usr/lib/aarch64-linux-android/libc++.so') { return $true }
    return $Path -match '^lib/clang/[^/]+/lib/linux/(libclang_rt\.builtins-aarch64-android\.a|aarch64/(libunwind|libatomic)\.a)$'
}

# Lists the NDK subset with each file's SHA-256, reading only the zip's central directory and the
# selected entries over HTTP. Cached, since the pinned zip never changes.
function Get-NdkSubset {
    $cached = Join-Path $CacheDir 'ndk-r29-subset.json'
    if (Test-Path $cached) { return [IO.File]::ReadAllText($cached) | ConvertFrom-Json }

    $tail = Read-RemoteRange $ndkUrl ($ndkSize - 65557) ($ndkSize - 1)
    $eocd = -1
    for ($i = $tail.Length - 22; $i -ge 0; $i--) {
        if ([BitConverter]::ToUInt32($tail, $i) -eq 0x06054b50) { $eocd = $i; break }
    }
    if ($eocd -lt 0) { throw 'No end of central directory in the NDK zip' }
    $entryCount = [BitConverter]::ToUInt16($tail, $eocd + 10)
    $directorySize = [BitConverter]::ToUInt32($tail, $eocd + 12)
    $directoryOffset = [BitConverter]::ToUInt32($tail, $eocd + 16)
    if ($entryCount -eq 0xFFFF -or $directoryOffset -eq 0xFFFFFFFF) { throw 'The NDK zip needs ZIP64, which this reader does not support' }
    $directory = Read-RemoteRange $ndkUrl $directoryOffset ($directoryOffset + $directorySize - 1)

    $entries = New-Object System.Collections.Generic.List[object]
    $at = 0
    for ($n = 0; $n -lt $entryCount; $n++) {
        if ([BitConverter]::ToUInt32($directory, $at) -ne 0x02014b50) { throw 'Corrupt NDK zip central directory' }
        $nameLength = [BitConverter]::ToUInt16($directory, $at + 28)
        $extraLength = [BitConverter]::ToUInt16($directory, $at + 30)
        $commentLength = [BitConverter]::ToUInt16($directory, $at + 32)
        $name = [Text.Encoding]::UTF8.GetString($directory, $at + 46, $nameLength)
        $entries.Add([pscustomobject]@{
            name = $name
            method = [BitConverter]::ToUInt16($directory, $at + 10)
            compressedSize = [long][BitConverter]::ToUInt32($directory, $at + 20)
            size = [long][BitConverter]::ToUInt32($directory, $at + 24)
            offset = [long][BitConverter]::ToUInt32($directory, $at + 42)
        })
        $at += 46 + $nameLength + $extraLength + $commentLength
    }
    $sorted = @($entries | Sort-Object offset)
    $selected = New-Object System.Collections.Generic.List[object]
    for ($i = 0; $i -lt $sorted.Count; $i++) {
        $entry = $sorted[$i]
        if (-not $entry.name.StartsWith($ndkPrefix) -or $entry.name.EndsWith('/')) { continue }
        $path = $entry.name.Substring($ndkPrefix.Length)
        if (-not (Test-NdkSubsetFile $path)) { continue }
        $end = if ($i + 1 -lt $sorted.Count) { $sorted[$i + 1].offset } else { $directoryOffset }
        $selected.Add([pscustomobject]@{ path = $path; entry = $entry; end = $end })
    }

    # Neighbouring entries are fetched together.
    $files = New-Object System.Collections.Generic.List[object]
    $i = 0
    while ($i -lt $selected.Count) {
        $j = $i
        while ($j + 1 -lt $selected.Count -and $selected[$j + 1].entry.offset - $selected[$j].end -lt 1MB) { $j++ }
        $chunkStart = $selected[$i].entry.offset
        $chunk = Read-RemoteRange $ndkUrl $chunkStart ($selected[$j].end - 1)
        for ($k = $i; $k -le $j; $k++) {
            $item = $selected[$k]
            $local = [int]($item.entry.offset - $chunkStart)
            if ([BitConverter]::ToUInt32($chunk, $local) -ne 0x04034b50) { throw "Corrupt NDK zip entry $($item.path)" }
            $dataStart = $local + 30 + [BitConverter]::ToUInt16($chunk, $local + 26) + [BitConverter]::ToUInt16($chunk, $local + 28)
            $compressed = New-Object IO.MemoryStream($chunk, $dataStart, [int]$item.entry.compressedSize)
            $content = New-Object IO.MemoryStream
            switch ($item.entry.method) {
                0 { $compressed.CopyTo($content) }
                8 {
                    $inflate = New-Object IO.Compression.DeflateStream($compressed, [IO.Compression.CompressionMode]::Decompress)
                    try { $inflate.CopyTo($content) } finally { $inflate.Dispose() }
                }
                default { throw "NDK zip entry $($item.path) uses compression method $($item.entry.method)" }
            }
            $bytes = $content.ToArray()
            if ($bytes.Length -ne $item.entry.size) { throw "NDK zip entry $($item.path) inflated to the wrong size" }
            $files.Add([pscustomobject]@{ path = $item.path; size = $item.entry.size; sha256 = Get-BytesSha256Hex $bytes })
        }
        $i = $j + 1
    }
    $subset = [pscustomobject]@{ url = $ndkUrl; size = $ndkSize; prefix = $ndkPrefix; digest = Get-ListDigest $files.ToArray(); files = $files.ToArray() }
    New-Item -ItemType Directory -Force $CacheDir | Out-Null
    [IO.File]::WriteAllText($cached, ($subset | ConvertTo-Json -Depth 4 -Compress), (New-Object Text.UTF8Encoding $false))
    return $subset
}

function Publish-Translator([string]$Destination, [string]$Work) {
    $sdks = (& $Dotnet --list-sdks) -join "`n"
    if ($sdks -notmatch '(?m)^10\.') { throw 'Publishing the translator for Android needs the .NET 10 SDK.' }
    # A staged copy retargeted to net10.0, so the checkout's own net8.0 build outputs stay untouched.
    $stage = Join-Path $Work 'translator-stage'
    if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
    foreach ($tree in 'translator\src', 'Launcher\WiiCompiled.Setup.Common') {
        & (Join-Path $env:SystemRoot 'System32\robocopy.exe') (Join-Path $repo $tree) (Join-Path $stage $tree) /E /XD bin obj /NFL /NDL /NJH /NJS /NP | Out-Null
        if ($LASTEXITCODE -ge 8) { throw "Staging $tree failed (robocopy $LASTEXITCODE)" }
    }
    # Build output the checkout may hold for another target framework would be picked up here and
    # make the retargeted publish resolve the wrong references.
    Get-ChildItem -Recurse -Directory $stage |
        Where-Object { $_.Name -eq 'bin' -or $_.Name -eq 'obj' } |
        ForEach-Object { if (Test-Path $_.FullName) { Remove-Item -Recurse -Force $_.FullName } }
    foreach ($project in Get-ChildItem -Recurse -Filter *.csproj $stage) {
        $text = [IO.File]::ReadAllText($project.FullName)
        $retargeted = $text.Replace('<TargetFramework>net8.0</TargetFramework>', '<TargetFramework>net10.0</TargetFramework>')
        if ($retargeted -eq $text) { throw "$($project.Name) does not target net8.0" }
        [IO.File]::WriteAllText($project.FullName, $retargeted)
    }
    if (Test-Path $Destination) { Remove-Item -Recurse -Force $Destination }
    & $Dotnet publish (Join-Path $stage 'translator\src\Translator.Cli\Translator.Cli.csproj') -c Release `
        -r linux-bionic-arm64 --self-contained true -p:PublishSingleFile=false -p:InvariantGlobalization=true `
        -p:DebugType=none -o $Destination | Out-Host
    if ($LASTEXITCODE -ne 0) { throw "Publishing the translator failed ($LASTEXITCODE)" }
    Get-ChildItem -Recurse -Filter *.pdb $Destination | Remove-Item
    if (-not (Test-Path (Join-Path $Destination 'libhostfxr.so'))) { throw 'The translator was not published self-contained' }
}

$work = Join-Path $CacheDir 'work'
New-Item -ItemType Directory -Force $work | Out-Null
# The tree is staged in the cache and packed into one deflated files.zip: Android's asset packaging
# never compresses .so files, and libLLVM alone is 134 MB.
$stage = Join-Path $work 'toolchain'
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
New-Item -ItemType Directory -Force $stage | Out-Null
if (Test-Path $OutputDir) { Remove-Item -Recurse -Force $OutputDir }
New-Item -ItemType Directory -Force $OutputDir | Out-Null

$llvm = Join-Path $stage 'llvm'
foreach ($package in $termuxPackages) { Expand-TermuxPackage $package $llvm $work }
$licenses = @($termuxPackages | ForEach-Object { "$($_.Name) $($_.File.Split('/')[-1].Replace('.deb', '')) - $($_.License) - https://github.com/termux/termux-packages" })
New-Item -ItemType Directory -Force (Join-Path $llvm 'licenses') | Out-Null
[IO.File]::WriteAllText((Join-Path $llvm 'licenses\PACKAGES.txt'), (($licenses -join "`n") + "`n"))

$translator = Join-Path $stage 'translator'
Publish-Translator $translator $work
# Static archives for embedding the runtime in a native app; the translator runs on the shared one.
Get-ChildItem -Filter *.a $translator | Remove-Item
& (Join-Path $NdkLlvm 'bin\clang.exe') --target=aarch64-linux-android29 -O2 -Wall -Wextra -Werror -pie -fPIE `
    -o (Join-Path $translator 'translator_host') (Join-Path $PSScriptRoot 'toolchain\translator_host.c')
if ($LASTEXITCODE -ne 0) { throw "Building translator_host failed ($LASTEXITCODE)" }

$ndk = Get-NdkSubset
if ($ndkSubsetDigest -and $ndk.digest -ne $ndkSubsetDigest) {
    throw "The NDK subset digest $($ndk.digest) does not match the pinned $ndkSubsetDigest"
}
[IO.File]::WriteAllText((Join-Path $OutputDir 'ndk.json'), ($ndk | ConvertTo-Json -Depth 4 -Compress), (New-Object Text.UTF8Encoding $false))

# .NET enumeration keeps the root as given (Get-ChildItem can expand an 8.3 TEMP name).
$root = [IO.Path]::GetFullPath($stage).TrimEnd('\')
$files = @([IO.Directory]::EnumerateFiles($root, '*', [IO.SearchOption]::AllDirectories) | ForEach-Object {
    [pscustomobject]@{ path = $_.Substring($root.Length + 1).Replace('\', '/'); size = (New-Object IO.FileInfo $_).Length; sha256 = Get-Sha256Hex $_ }
} | Sort-Object path)
$zip = [IO.Compression.ZipFile]::Open((Join-Path $OutputDir 'files.zip'), 'Create')
try {
    foreach ($file in $files) {
        [void][IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip, (Join-Path $root $file.path), $file.path, 'Optimal')
    }
} finally { $zip.Dispose() }

$toolchain = [ordered]@{
    schema = 1
    fingerprint = Get-ListDigest $files
    archive = 'files.zip'
    ndk = 'ndk.json'
    files = $files
}
[IO.File]::WriteAllText((Join-Path $OutputDir 'toolchain.json'), ($toolchain | ConvertTo-Json -Depth 4), (New-Object Text.UTF8Encoding $false))
$unpacked = [math]::Round((($files | Measure-Object size -Sum).Sum) / 1MB)
$packed = [math]::Round((New-Object IO.FileInfo (Join-Path $OutputDir 'files.zip')).Length / 1MB)
Write-Host "Quest toolchain $($toolchain.fingerprint) ($unpacked MB, $packed MB packed; NDK subset $($ndk.digest))"
