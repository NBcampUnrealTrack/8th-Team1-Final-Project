# Multi-User Editing 관찰자 인스턴스 (AI 에이전트용)
#
# 세션에 참가해 다른 사람의 작업을 관찰/보고하는 별도 에디터 인스턴스.
# 표시 이름에 -Agent 접미사가 붙어 세션 히스토리에서 사람과 구분된다.
#
# 쓰기 금지는 엔진이 아니라 Tools/MultiUser/mu_guard.py 가드레일로 강제한다.
# (엔진의 ReadOnlyAssignment 는 VirtualProduction 역할 시스템에 의존해서 채택하지 않았다)
#
# 실행: Start-MU-Observer.bat <세션이름>

param(
    [Parameter(Mandatory = $false)]
    [string]$SessionName
)

. (Join-Path $PSScriptRoot '_Common.ps1')

$cfg       = Get-HostConfig
$editorExe = Assert-EngineFile 'Engine\Binaries\Win64\UnrealEditor.exe' '에디터'

if (-not $SessionName) {
    Stop-WithError @(
        '[오류] 참가할 세션 이름이 필요합니다.',
        '',
        '       사용법: Start-MU-Observer.bat <세션이름>',
        '       세션 이름은 사람이 쓰는 에디터의 Multi-User Browser 에서 확인할 수 있습니다.'
    )
}

Write-Host '사용 가능한 경로를 확인합니다...' -ForegroundColor DarkGray
$transport = Select-Transport $cfg

$displayName = "$($env:USERNAME)-Agent"
$iniPath = Write-MultiUserConfig -Transport $transport -Config $cfg -DisplayName $displayName

Write-Host ''
Write-Host ("  경로       : {0}  ({1})" -f $transport.Name, $transport.LocalAddress) -ForegroundColor White
Write-Host ("  세션       : {0} @ {1}" -f $SessionName, $cfg.serverName) -ForegroundColor White
Write-Host ("  표시 이름  : {0}" -f $displayName) -ForegroundColor Yellow
Write-Host ("  설정 기록  : {0}" -f $iniPath) -ForegroundColor DarkGray
Write-Host ''
Write-Host '  관찰자 인스턴스를 시작합니다. 이 인스턴스로는 에셋을 수정하지 마세요.' -ForegroundColor Cyan
Write-Host ''

# 관찰자는 지정된 세션에 자동 참가한다. 사람이 쓰는 에디터와 달리 세션을 새로
# 만들지 않도록, 이미 존재하는 세션 이름을 인자로 받는 것이 전제다.
Start-Process -FilePath $editorExe -ArgumentList @(
    "`"$ProjectFile`"",
    "-CONCERTSERVER=$($cfg.serverName)",
    "-CONCERTSESSION=$SessionName",
    "-CONCERTAUTOCONNECT",
    "-CONCERTRETRYAUTOCONNECTONERROR",
    "-UDPMESSAGING_TRANSPORT_UNICAST=$($transport.LocalAddress):0",
    "-nosplash"
)
