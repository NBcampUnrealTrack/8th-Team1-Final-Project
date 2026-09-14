# Multi-User Editing 스크립트 공용 함수.
# Start-MU-Server.ps1 / Start-MU-Editor.ps1 / Start-MU-Observer.ps1 이 dot-source 로 불러온다.
#
# 호스트 주소와 경로 우선순위는 코드가 아니라 host.json 에 있다.

$script:ToolsDir    = $PSScriptRoot
$script:ProjectDir  = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$script:ProjectFile = Join-Path $ProjectDir 'MarineDemolition.uproject'

# 팀원이 고칠 수 있는 값 (엔진 경로가 다른 사람만)
$script:EngineDir = 'E:\Unreal\git\UE_5.8'


function Stop-WithError {
    param([string[]]$Lines)

    Write-Host ''
    foreach ($line in $Lines) { Write-Host $line -ForegroundColor Red }
    Write-Host ''

    # 더블클릭 실행 시 창이 바로 닫히지 않게 대기한다.
    # 자동화/파이프에서 호출된 경우엔 멈추지 않고 그냥 종료한다.
    if ([Environment]::UserInteractive -and -not [Console]::IsInputRedirected) {
        Read-Host '엔터를 누르면 창이 닫힙니다' | Out-Null
    }
    exit 1
}


function Get-HostConfig {
    $path = Join-Path $ToolsDir 'host.json'
    if (-not (Test-Path $path)) {
        Stop-WithError @('[오류] host.json 을 찾지 못했습니다.', "       $path")
    }
    try {
        return Get-Content $path -Raw -Encoding UTF8 | ConvertFrom-Json
    }
    catch {
        Stop-WithError @('[오류] host.json 파싱 실패.', "       $_")
    }
}


function Get-LocalAddressFor {
    <#
        transport 정의에 맞는 이 PC의 IPv4 주소를 반환한다. 없으면 $null.
        어댑터 이름이 바뀌어도 동작하도록 어댑터 매칭 → 서브넷 스캔 순으로 시도한다.
    #>
    param($Transport)

    $ErrorActionPreference = 'SilentlyContinue'

    $adapter = Get-NetAdapter |
        Where-Object { $_.InterfaceDescription -like $Transport.adapterMatch -and $_.Status -eq 'Up' } |
        Select-Object -First 1

    if (-not $adapter) {
        $adapter = Get-NetAdapter |
            Where-Object { $_.Name -like $Transport.adapterMatch -and $_.Status -eq 'Up' } |
            Select-Object -First 1
    }

    if ($adapter) {
        $ip = (Get-NetIPAddress -AddressFamily IPv4 -InterfaceIndex $adapter.ifIndex |
               Select-Object -First 1).IPAddress
        if ($ip) { return $ip }
    }

    return (Get-NetIPAddress -AddressFamily IPv4 |
            Where-Object { $_.IPAddress -like "$($Transport.subnetPrefix)*" } |
            Select-Object -First 1).IPAddress
}


function Select-Transport {
    <#
        host.json 의 transports 를 순서대로 시도해 쓸 수 있는 첫 경로를 고른다.
        반환: @{ Name; LocalAddress; HostAddress; IsHost }
        전부 실패하면 각 경로가 어디서 막혔는지 출력하고 종료한다.
    #>
    param($Config)

    $amHost = ($env:COMPUTERNAME -eq $Config.hostMachine)
    $failures = @()

    foreach ($t in $Config.transports) {
        Write-Host ("  {0,-10} 확인 중..." -f $t.name) -NoNewline -ForegroundColor DarkGray

        $localIP = Get-LocalAddressFor $t
        if (-not $localIP) {
            Write-Host ' 어댑터 없음' -ForegroundColor DarkYellow
            $failures += "  - $($t.name): 어댑터가 없거나 꺼져 있음 ($($t.adapterMatch))"
            continue
        }

        # 내가 호스트면 자기 자신에게 ping 할 필요가 없다.
        if (-not $amHost) {
            if (-not (Test-Connection -ComputerName $t.hostAddress -Count 1 -Quiet -ErrorAction SilentlyContinue)) {
                Write-Host (' 호스트 {0} 응답 없음' -f $t.hostAddress) -ForegroundColor DarkYellow
                $failures += "  - $($t.name): 내 주소는 $localIP 인데 호스트 $($t.hostAddress) 에 닿지 않음"
                continue
            }
        }

        Write-Host (' OK ({0})' -f $localIP) -ForegroundColor Green
        return @{
            Name         = $t.name
            LocalAddress = $localIP
            HostAddress  = $t.hostAddress
            IsHost       = $amHost
        }
    }

    Stop-WithError (@('[오류] 사용할 수 있는 VPN 경로가 없습니다.', '') + $failures + @(
        '',
        '       Radmin VPN 또는 Tailscale 을 실행하고 팀 네트워크에 접속한 뒤 다시 시도하세요.',
        '       호스트 주소가 바뀌었다면 Tools\MultiUser\host.json 을 고쳐야 합니다.'
    ))
}


function Write-MultiUserConfig {
    <#
        선택된 경로를 머신별 설정에 기록한다.
        Saved/ 는 .gitignore 라 팀 공유 파일을 건드리지 않는다.

        UdpMessaging 섹션 전체를 매번 다시 써서, 이전 실행이 남긴 반대편 경로의
        StaticEndpoints 가 죽은 엔드포인트로 남지 않게 한다.
    #>
    param($Transport, $Config, [string]$DisplayName)

    $dir = Join-Path $ProjectDir 'Saved\Config\WindowsEditor'
    if (-not (Test-Path $dir)) { New-Item -ItemType Directory -Path $dir -Force | Out-Null }

    $iniPath  = Join-Path $dir 'Engine.ini'
    $endpoint = "$($Transport.HostAddress):$($Config.serverPort)"

    $existing = if (Test-Path $iniPath) { Get-Content $iniPath -Encoding UTF8 } else { @() }

    # 이 스크립트가 관리하는 블록만 걷어낸다.
    $kept = @()
    $inOurBlock = $false
    foreach ($line in $existing) {
        if ($line -match '^\s*;\s*<<< MultiUser launcher') { $inOurBlock = $true;  continue }
        if ($line -match '^\s*;\s*>>> MultiUser launcher') { $inOurBlock = $false; continue }
        if (-not $inOurBlock) { $kept += $line }
    }

    $block = @(
        '; <<< MultiUser launcher — 이 블록은 Start-MU-*.ps1 이 매번 덮어쓴다. 직접 고치지 말 것.',
        "; 선택된 경로: $($Transport.Name)  (내 주소 $($Transport.LocalAddress) → 호스트 $endpoint)",
        '[/Script/UdpMessaging.UdpMessagingSettings]',
        "UnicastEndpoint=$($Transport.LocalAddress):0",
        "!StaticEndpoints=ClearArray",
        "+StaticEndpoints=$endpoint",
        '',
        '[/Script/ConcertClient.ConcertClientConfig]',
        "ClientSettings=(DisplayName=`"$DisplayName`",DiscoveryTimeoutSeconds=30)",
        '; >>> MultiUser launcher'
    )

    # PowerShell 5.1 의 UTF8 은 BOM 을 붙이지만 UE ini 파서는 BOM 을 허용한다.
    ($kept + $block) | Set-Content -Path $iniPath -Encoding UTF8

    return $iniPath
}


function Assert-EngineFile {
    param([string]$RelativePath, [string]$What)

    $full = Join-Path $EngineDir $RelativePath
    if (-not (Test-Path $full)) {
        Stop-WithError @(
            "[오류] $What 실행 파일이 없습니다.",
            "       $full",
            '',
            '       엔진 경로가 다르면 _Common.ps1 상단의 $EngineDir 을 고치세요.',
            '       소스 빌드라면 해당 타겟을 먼저 빌드해야 합니다.'
        )
    }
    return $full
}
