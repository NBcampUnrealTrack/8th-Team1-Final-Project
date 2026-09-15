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


function Test-TailnetPeer {
    <#
        주어진 주소가 이 tailnet 의 피어 목록에 있는지 본다.
        $true  = 목록에 있음
        $false = 목록에 없음 (다른 tailnet 일 가능성)
        $null  = tailscale CLI 를 못 찾아 판단 불가
    #>
    param([string]$Address)

    $ErrorActionPreference = 'SilentlyContinue'

    $cli = (Get-Command tailscale -ErrorAction SilentlyContinue).Source
    if (-not $cli) {
        foreach ($c in @("$env:ProgramFiles\Tailscale\tailscale.exe",
                         "${env:ProgramFiles(x86)}\Tailscale\tailscale.exe")) {
            if (Test-Path $c) { $cli = $c; break }
        }
    }
    if (-not $cli) { return $null }

    $out = & $cli status 2>$null
    if (-not $out) { return $null }

    return [bool](($out -join "`n") -match [regex]::Escape($Address))
}


function Get-StaticEndpoints {
    <#
        호스트 주소와 포트의 **유일한 출처**는 Config/DefaultEngine.ini 의
        +StaticEndpoints 다. 에디터가 실제로 읽는 파일이 그것이기 때문에,
        런처가 따로 값을 들고 있으면 둘이 어긋났을 때 런처는 통과하는데
        에디터만 서버를 못 찾는 형태로 나타난다. 그래서 여기서 읽어 쓴다.

        반환: @( @{ Address; Port } )
    #>
    $ini = Join-Path $ProjectDir 'Config\DefaultEngine.ini'
    if (-not (Test-Path $ini)) {
        Stop-WithError @('[오류] Config/DefaultEngine.ini 를 찾지 못했습니다.', "       $ini")
    }

    $found = @()
    foreach ($line in (Get-Content $ini)) {
        # 주석(;)으로 시작하는 줄은 정규식이 자연히 걸러낸다.
        if ($line -match '^\s*\+StaticEndpoints\s*=\s*([0-9]{1,3}(?:\.[0-9]{1,3}){3})\s*:\s*([0-9]+)') {
            $found += [pscustomobject]@{ Address = $Matches[1]; Port = [int]$Matches[2] }
        }
    }

    if ($found.Count -eq 0) {
        Stop-WithError @(
            '[오류] Config/DefaultEngine.ini 에 +StaticEndpoints 가 없습니다.',
            '',
            '       Tailscale 은 멀티캐스트를 나르지 않아 자동 탐색이 불가능합니다.',
            '       호스트 주소를 직접 지정해야 붙습니다. 예:',
            '         +StaticEndpoints=100.77.222.84:50002',
            '',
            '       호스트에서 서버 런처를 실행하면 넣을 값이 출력됩니다.'
        )
    }

    return $found
}


function Select-Transport {
    <#
        host.json 의 transports 를 순서대로 시도해 쓸 수 있는 첫 경로를 고른다.
        각 경로의 호스트 주소는 DefaultEngine.ini 의 +StaticEndpoints 중
        subnetPrefix 가 맞는 것을 쓴다 (경로가 여러 개면 줄도 여러 개다).

        반환: @{ Name; LocalAddress; HostAddress; HostPort; IsHost }
    #>
    param($Config)

    $endpoints = Get-StaticEndpoints
    $amHost    = ($env:COMPUTERNAME -eq $Config.hostMachine)
    $failures  = @()

    foreach ($t in $Config.transports) {
        Write-Host ("  {0,-10} 확인 중..." -f $t.name) -NoNewline -ForegroundColor DarkGray

        $ep = $endpoints | Where-Object { $_.Address.StartsWith($t.subnetPrefix) } | Select-Object -First 1
        if (-not $ep) {
            Write-Host ' 엔드포인트 없음' -ForegroundColor DarkYellow
            $failures += "  - $($t.name): DefaultEngine.ini 에 $($t.subnetPrefix)* 로 시작하는 +StaticEndpoints 가 없음"
            continue
        }

        $localIP = Get-LocalAddressFor $t
        if (-not $localIP) {
            Write-Host ' 어댑터 없음' -ForegroundColor DarkYellow
            $failures += "  - $($t.name): 어댑터가 없거나 꺼져 있음 ($($t.adapterMatch))"
            continue
        }

        # 내가 호스트면 자기 자신에게 ping 할 필요가 없다.
        if (-not $amHost) {
            if (-not (Test-Connection -ComputerName $ep.Address -Count 1 -Quiet -ErrorAction SilentlyContinue)) {
                Write-Host (' 호스트 {0} 응답 없음' -f $ep.Address) -ForegroundColor DarkYellow

                # 흔한 원인: 호스트가 아예 다른 tailnet(다른 계정)에 로그인되어 있다.
                # 그러면 ping 이 물리 네트워크로 새어나가 TTL 만료로 죽는다.
                if ((Test-TailnetPeer $ep.Address) -eq $false) {
                    $failures += "  - $($t.name): 호스트 $($ep.Address) 가 이 tailnet 에 없습니다."
                    $failures += "      tailscale status 목록에 안 보입니다. 호스트가 다른 계정/tailnet 에"
                    $failures += "      로그인되어 있을 수 있습니다. 같은 tailnet 으로 맞춰야 합니다."
                }
                else {
                    $failures += "  - $($t.name): 내 주소는 $localIP 인데 호스트 $($ep.Address) 에 닿지 않음"
                }
                continue
            }
        }

        Write-Host (' OK ({0})' -f $localIP) -ForegroundColor Green
        return @{
            Name         = $t.name
            LocalAddress = $localIP
            HostAddress  = $ep.Address
            HostPort     = $ep.Port
            IsHost       = $amHost
        }
    }

    Stop-WithError (@('[오류] 사용할 수 있는 VPN 경로가 없습니다.', '') + $failures + @(
        '',
        '       확인 순서:',
        '         1) tailscale status  - 호스트가 목록에 보이는가',
        '         2) 안 보이면 호스트와 내가 같은 tailnet(같은 계정)인지 확인',
        '         3) 호스트 주소가 바뀌었다면 Config/DefaultEngine.ini 의',
        '            +StaticEndpoints 를 고쳐 커밋 (여기가 유일한 출처)'
    ))
}


# [제거됨 - 2026-09-15] Write-MultiUserConfig
#
# Saved/Config/<플랫폼>Editor/Engine.ini 에 StaticEndpoints 를 주입하려던 함수였다.
# **동작하지 않는다.** 에디터가 그 파일을 시작 시점에 통째로 덮어써서, 써넣은 값이
# 읽히기 전에 사라진다 (에디터 실행 중에 열어보면 블록이 이미 없고, 서버 탐색이
# 영영 실패했다).
#
# StaticEndpoints 는 커맨드라인 옵션도 없으므로 Config/DefaultEngine.ini 가 유일한
# 주입 경로다. 호스트 주소는 머신마다 다르지 않아 공유 파일에 둬도 문제없다.
# 머신별로 다른 UnicastEndpoint 만 -UDPMESSAGING_TRANSPORT_UNICAST= 로 넘긴다.
#
# 설정 주입 지점을 다시 찾게 되면 "Saved/Config 는 아니다"를 기억할 것.


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
