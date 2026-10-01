# Multi-User Editing 서버 실행 (호스트 PC 전용)
#
# 서버는 0.0.0.0 에 묶는다. 이렇게 하면 Radmin(26.x)과 Tailscale(100.x) 양쪽에서
# 동시에 도달할 수 있어, 클라이언트가 어느 경로로 붙든 같은 세션을 쓴다.
# (netstat 으로 0.0.0.0:<포트> 와일드카드 바인딩 실측 확인)
#
# 실행: Start-MU-Server.bat 을 더블클릭
# 이 창은 세션이 끝날 때까지 켜 두어야 한다.

. (Join-Path $PSScriptRoot '_Common.ps1')

$cfg       = Get-HostConfig
$serverExe = Assert-EngineFile 'Engine\Binaries\Win64\UnrealMultiUserSlateServer.exe' '멀티유저 서버'

# 호스트 1대 고정 — 다른 PC에서 서버를 띄우면 팀이 두 개의 서버로 갈린다.
if ($env:COMPUTERNAME -ne $cfg.hostMachine) {
    Stop-WithError @(
        '[오류] 이 PC는 지정된 호스트가 아닙니다.',
        "       지정 호스트 : $($cfg.hostMachine)",
        "       이 PC       : $($env:COMPUTERNAME)",
        '',
        '       서버는 한 대에서만 띄웁니다. 호스트를 바꾸려면 host.json 의 hostMachine 과',
        '       Config/DefaultEngine.ini 의 +StaticEndpoints 를 고치고 커밋하세요.'
    )
}

$endpoints = Get-StaticEndpoints
$port      = $endpoints[0].Port

Write-Host ''
Write-Host '============================================================' -ForegroundColor Cyan
Write-Host ("  서버 이름 : {0}" -f $cfg.serverName) -ForegroundColor White
Write-Host ("  포트      : {0}  (0.0.0.0 / 모든 경로에서 수신)" -f $port) -ForegroundColor White
Write-Host '============================================================' -ForegroundColor Cyan
Write-Host ''
Write-Host '  이 PC의 주소가 DefaultEngine.ini 의 +StaticEndpoints 와 맞는지 확인:' -ForegroundColor White
Write-Host ''

foreach ($t in $cfg.transports) {
    $mine     = Get-LocalAddressFor $t
    $expected = ($endpoints | Where-Object { $_.Address.StartsWith($t.subnetPrefix) } | Select-Object -First 1).Address

    if (-not $mine) {
        Write-Host ("    {0,-10} (어댑터 꺼짐)" -f $t.name) -ForegroundColor DarkGray
    }
    elseif ($mine -eq $expected) {
        Write-Host ("    {0,-10} {1}" -f $t.name, $mine) -ForegroundColor Green
    }
    else {
        Write-Host ("    {0,-10} {1}" -f $t.name, $mine) -ForegroundColor Yellow
        Write-Host ("               <-- DefaultEngine.ini 에는 {0} 로 적혀 있습니다. 고쳐서 커밋하세요." -f $expected) -ForegroundColor Yellow
    }
}

Write-Host ''
Write-Host '============================================================' -ForegroundColor Cyan
Write-Host ''

& $serverExe "-CONCERTSERVER=$($cfg.serverName)" "-UDPMESSAGING_TRANSPORT_UNICAST=0.0.0.0:$port"
