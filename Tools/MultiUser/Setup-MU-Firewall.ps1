# Multi-User Editing 용 Windows 방화벽 설정 (1회 실행, 관리자 권한 필요)
#
# 조사해 보면 "서버가 목록에 안 보인다" 신고의 상당수가 플러그인 문제가 아니라
# 방화벽 인바운드 차단 또는 Radmin 어댑터가 '공용 네트워크'로 잡힌 경우다.
#
# 실행: 이 파일 우클릭 > PowerShell에서 실행  (또는 관리자 PowerShell에서)
#       powershell -ExecutionPolicy Bypass -File .\Setup-MU-Firewall.ps1

#Requires -RunAsAdministrator

$ErrorActionPreference = 'Stop'

# 팀원이 고칠 수 있는 값
$EngineDir = 'E:\Unreal\git\UE_5.8'

$targets = @(
    @{ Name = 'UE MultiUser - Editor';       Path = Join-Path $EngineDir 'Engine\Binaries\Win64\UnrealEditor.exe' }
    @{ Name = 'UE MultiUser - Server';       Path = Join-Path $EngineDir 'Engine\Binaries\Win64\UnrealMultiUserSlateServer.exe' }
    @{ Name = 'UE MultiUser - Server (CLI)'; Path = Join-Path $EngineDir 'Engine\Binaries\Win64\UnrealMultiUserServer.exe' }
)

Write-Host '=== 방화벽 규칙 생성 ===' -ForegroundColor Cyan

foreach ($t in $targets) {
    if (-not (Test-Path $t.Path)) {
        Write-Host ("  건너뜀 (파일 없음): {0}" -f $t.Path) -ForegroundColor DarkGray
        continue
    }

    foreach ($proto in @('UDP', 'TCP')) {
        $ruleName = "{0} ({1})" -f $t.Name, $proto

        if (Get-NetFirewallRule -DisplayName $ruleName -ErrorAction SilentlyContinue) {
            Write-Host ("  이미 있음: {0}" -f $ruleName) -ForegroundColor DarkGray
            continue
        }

        New-NetFirewallRule -DisplayName $ruleName `
                            -Direction Inbound `
                            -Action Allow `
                            -Program $t.Path `
                            -Protocol $proto `
                            -Profile Any | Out-Null

        Write-Host ("  생성됨: {0}" -f $ruleName) -ForegroundColor Green
    }
}

Write-Host ''
Write-Host '=== Radmin 어댑터 네트워크 프로필 확인 ===' -ForegroundColor Cyan

$radmin = Get-NetAdapter | Where-Object { $_.InterfaceDescription -like '*Radmin*' -and $_.Status -eq 'Up' }

if (-not $radmin) {
    Write-Host '  Radmin VPN 어댑터를 찾지 못했습니다. Radmin을 켠 뒤 다시 실행하세요.' -ForegroundColor Yellow
}
else {
    foreach ($a in $radmin) {
        $profile = Get-NetConnectionProfile -InterfaceIndex $a.ifIndex -ErrorAction SilentlyContinue
        if (-not $profile) { continue }

        if ($profile.NetworkCategory -eq 'Public') {
            Write-Host ("  '{0}' 이(가) 공용 네트워크입니다. 개인으로 바꿉니다." -f $profile.Name) -ForegroundColor Yellow
            Set-NetConnectionProfile -InterfaceIndex $a.ifIndex -NetworkCategory Private
            Write-Host '  변경 완료.' -ForegroundColor Green
        }
        else {
            Write-Host ("  '{0}' : {1} — 정상" -f $profile.Name, $profile.NetworkCategory) -ForegroundColor Green
        }
    }
}

Write-Host ''
Write-Host '완료. Tools\MultiUser\Start-MU-Editor.bat 으로 에디터를 실행하세요.' -ForegroundColor Cyan
