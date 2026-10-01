# 엔진 빌드 지문 출력 (Windows)
#
# 멀티유저 세션 전 체크리스트 1번. 참가자 전원이 이걸 돌려서 "엔진 지문" 줄이
# 글자 그대로 같은지 확인한다. 다르면 접속은 되어도 트랜잭션이 어긋난다.
#
# macOS 는 check-engine-version.command 를 쓴다. 출력 형식은 동일하다.

. (Join-Path $PSScriptRoot '_Common.ps1')

$versionFile = Join-Path $EngineDir 'Engine\Build\Build.version'

if (-not (Test-Path $versionFile)) {
    Stop-WithError @(
        '[오류] Build.version 을 찾지 못했습니다.',
        "       $versionFile",
        '',
        '       엔진 경로가 다르면 _Common.ps1 상단의 $EngineDir 을 고치세요.'
    )
}

$v = Get-Content $versionFile -Raw | ConvertFrom-Json

# Concert 가 비교하는 값들 (ConcertVersion.h 의 FConcertEngineVersionInfo).
# CompatibleChangelist 는 바이너리 호환성의 기준이다.
$fingerprint = "{0}.{1}.{2}-{3}-L{4}-{5}" -f `
    $v.MajorVersion, $v.MinorVersion, $v.PatchVersion,
    $v.CompatibleChangelist, $v.IsLicenseeVersion, $v.BranchName

# 엔진이 git 체크아웃이면 커밋도 같이 보여준다.
$head = ''
try {
    $head = (& git -C $EngineDir rev-parse --short HEAD 2>$null)
} catch { }

Write-Host ''
Write-Host '============================================================' -ForegroundColor Cyan
Write-Host '  엔진 지문 (이 줄이 전원 동일해야 함)' -ForegroundColor White
Write-Host ''
Write-Host "    $fingerprint" -ForegroundColor Yellow
Write-Host ''
Write-Host '============================================================' -ForegroundColor Cyan
Write-Host ''
Write-Host ("  엔진 경로       : {0}" -f $EngineDir) -ForegroundColor DarkGray
if ($head) {
    Write-Host ("  엔진 git HEAD   : {0}" -f $head) -ForegroundColor DarkGray
}
Write-Host ("  Changelist      : {0}" -f $v.Changelist) -ForegroundColor DarkGray
Write-Host ("  IsPromotedBuild : {0}" -f $v.IsPromotedBuild) -ForegroundColor DarkGray
Write-Host ''
Write-Host '  지문은 같은데 Changelist 만 다르면 소스 빌드와 런처 빌드를 섞은 경우다.' -ForegroundColor DarkGray
Write-Host '  그때는 Multi-User 설정의 Support Mixed Build Types 를 켜야 할 수 있다.' -ForegroundColor DarkGray
Write-Host ''

if ([Environment]::UserInteractive -and -not [Console]::IsInputRedirected) {
    Read-Host '엔터를 누르면 창이 닫힙니다' | Out-Null
}
