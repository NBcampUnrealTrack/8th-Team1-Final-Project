# Multi-User Editing 세션용 에디터 실행 (참가자 전원)
#
# Radmin 을 우선 쓰고, 죽어 있으면 Tailscale 로 넘어간다. 우선순위는 host.json 의
# transports 순서다. 선택된 경로는 머신별 설정(Saved/Config/WindowsEditor/Engine.ini)에
# 기록되므로, 경로가 바뀌면 이 스크립트를 다시 실행하기만 하면 된다.
#
# 실행: Start-MU-Editor.bat 을 더블클릭
# 멀티유저 세션을 쓰지 않을 때는 평소처럼 .uproject 를 더블클릭해도 된다.

. (Join-Path $PSScriptRoot '_Common.ps1')

$cfg       = Get-HostConfig
$editorExe = Assert-EngineFile 'Engine\Binaries\Win64\UnrealEditor.exe' '에디터'

if (-not (Test-Path $ProjectFile)) {
    Stop-WithError @(
        '[오류] MarineDemolition.uproject 를 찾지 못했습니다.',
        "       $ProjectFile",
        '       이 스크립트는 프로젝트의 Tools\MultiUser\ 안에 있어야 합니다.'
    )
}

Write-Host '사용 가능한 경로를 확인합니다...' -ForegroundColor DarkGray
$transport = Select-Transport $cfg

$displayName = $env:USERNAME

# StaticEndpoints 는 Config/DefaultEngine.ini 에 있다 (커밋됨).
# Saved/Config/<플랫폼>Editor/Engine.ini 에 써도 에디터가 시작하면서 지워버린다 —
# Write-MultiUserConfig 주석 참고.

Write-Host ''
Write-Host ("  경로       : {0}" -f $transport.Name) -ForegroundColor White
Write-Host ("  내 주소    : {0}" -f $transport.LocalAddress) -ForegroundColor White
Write-Host ("  호스트     : {0}:{1}" -f $transport.HostAddress, $cfg.serverPort) -ForegroundColor White
Write-Host ("  표시 이름  : {0}" -f $displayName) -ForegroundColor White
Write-Host ("  정적 엔드포인트: Config/DefaultEngine.ini") -ForegroundColor DarkGray
Write-Host ''
Write-Host '  에디터를 시작합니다. 툴바의 Multi-User 버튼으로 세션에 참가하세요.' -ForegroundColor Cyan
Write-Host ''

# 자동 연결(-CONCERTAUTOCONNECT)은 일부러 켜지 않는다.
# 연결 시도가 겹치면 앞선 시도가 취소되면서 'Connection Process Canceled' 로
# 세션이 만들어졌다가 롤백되는 것을 확인했다. 참가는 사용자가 툴바에서 한 번만 누른다.
Start-Process -FilePath $editorExe -ArgumentList @(
    "`"$ProjectFile`"",
    "-CONCERTSERVER=$($cfg.serverName)",
    "-UDPMESSAGING_TRANSPORT_UNICAST=$($transport.LocalAddress):0"
)
