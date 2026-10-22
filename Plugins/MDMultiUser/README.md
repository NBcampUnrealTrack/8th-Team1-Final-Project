# 함께 편집 — Multi-User 작업 공간

`feat/multi-user-editing`의 Concert 설정 및 실행 도구와 함께 사용하는 프로젝트 전용 에디터 패널입니다.

## 자동 체크포인트 — 2026-10-22

큰 맵의 내보내기 검증은 T3D 액터 헤더를 한 번 순회해 이름 집합으로 비교합니다. 실패한 작업도 완료 시점에서 5분 후 재시도해 긴 실패 직후 백업이 반복 실행되는 현상을 방지합니다.

Unreal의 `CopyActors`가 내보내지 않는 `GroupActor`는 `manifest.json`의 `groups`에 GUID·이름·클래스·잠금·직접 멤버·하위 그룹을 별도 기록합니다. `actor_count`는 T3D의 일반 액터 수이며 `group_count`는 별도 그룹 수입니다. 그룹 색인이 없는 기존 체크포인트도 읽습니다. 손상된 색인이나 T3D·기본 맵 체크섬 불일치는 복구 준비를 거부합니다.

복구 준비는 검증된 `base.umap`, `actors.t3d`, 그룹 정보가 포함된 `manifest.json`을 별도 폴더로 복사합니다. 현재 세션에 액터나 그룹을 자동 삽입하지 않으며, 그룹 정보는 복구본 비교와 수동 재구성에 사용합니다. 체크포인트는 원본 맵 저장이나 Concert Persist를 실행하지 않습니다. 전체 액터 내보내기·파일 기록은 메인 스레드에서 실행되므로 큰 맵에서는 짧은 정지가 남을 수 있습니다.

회귀 검사: `MDMultiUser.Safety.ActorExportIndex`는 큰 문자열과 누락 검색, `GroupCheckpoint`는 중첩 그룹·잠금·복구 복사·손상 거부·기존 형식 호환성을 검사합니다. `GroupCheckpoint`는 `/Engine/Maps/Entry`, `OceanCheckpointTiming`은 `/Game/Maps/L_Ocean`을 연 독립 에디터에서 `-MDMultiUserSafetyTest`로만 실행합니다. 테스트 백업은 `Saved/MultiUserRecovery/Automation/<PID>`에 분리됩니다.

UE 5.8.3 Win64 Development Editor 빌드 및 위 3개 검사 통과. 실제 Ocean 검사에서 일반 액터 15,496개와 그룹 88개를 8.782초에 체크포인트로 기록하고 별도 복구본을 검증했습니다. 액터 수와 맵 dirty 상태는 유지됐습니다. 큰 문자열 색인 검사는 약 2,100만 문자·15,000회 조회를 검증합니다. 이 수치는 독립 에디터의 1회 검사이며 장시간 협업 세션 전체 성능을 보장하지 않습니다.

## 열기

에디터 툴바의 **함께 편집** 또는 **창 → 함께 편집**을 선택합니다. 콘솔 명령은 `MD.MultiUser.Open`입니다. 탭을 우측에 도킹해서 디테일 패널처럼 사용할 수 있습니다.

## 화면

- 상단: 실제 Concert 연결 상태, 연결된 세션 이름, 세션 선택 또는 상세 버튼.
- 연결 전: 팀 작업 시작 순서와 세션이 보이지 않을 때의 안내.
- 연결 후: 본인을 맨 위에 표시하는 참여자 목록, 아바타 색상, 기기·플랫폼, 이름·기기 검색.
- 공통: 로컬 에디터에 열린 레벨과 경로 툴팁, Multi-User 설정, 팀 작업 가이드.
- 세션 나가기: `IMultiUserClientModule::DisconnectSession(true)`를 사용해 Unreal의 저장 확인 절차를 유지합니다.
- 세션 이름 복사: 클립보드 복사 후 2초 동안 완료 표시.

서버/세션 선택, 생성, 활동 내역과 저장 처리는 기존 Multi-User 브라우저에서 수행합니다. 이 플러그인은 엔진의 기본 브라우저 소스를 교체하지 않습니다. 연결 상태는 동기화 완료나 VPN 상태의 검증 결과를 의미하지 않습니다. 참가자별 편집 레벨·지연 시간처럼 조회하지 않은 정보는 표시하지 않습니다.

## 구현

`MDMultiUser`는 `PostEngineInit`에서 로드되는 Editor 모듈입니다. 공개 Concert API만 사용하며 엔진 설치 파일을 수정하지 않습니다. UI는 0.25초 간격으로 상태를 읽고, 참여자 정보가 달라질 때만 목록을 갱신합니다. 탭의 활성 타이머와 콜백은 Slate 공유 포인터 수명을 따릅니다. 모듈 종료 시 메뉴·콘솔 명령·탭 등록을 해제합니다.

`SMDMultiUserWorkspace.cpp`의 `MDWorkspace` 네임스페이스에서 색상과 카드 모양을 관리합니다. 화면 문자열은 `MDLOC`와 `MDWorkspacePreferences.cpp`의 한국어·영어 문구 목록으로 관리합니다. 에디터 전체 문화권 설정을 변경하지 않습니다. 최초 독립 창 크기는 460 × 720입니다.

## 빌드

Windows 프로젝트 빌드:

```powershell
& E:/Unreal/git/UE_5.8/Engine/Build/BatchFiles/Build.bat MarineDemolitionEditor Win64 Development -Project=E:/Unreal/ProjectATM/MarineDemolition.uproject -WaitMutex -NoHotReloadFromIDE
```

macOS에서는 같은 소스를 해당 UE 5.8 에디터 타깃으로 빌드해야 합니다. 생성된 Windows DLL은 커밋 대상이 아닙니다.

## 검증 기록 — 2026-09-15

- UE 5.8 / Win64 Development: 최종 전체 `MarineDemolitionEditor` 빌드 성공.
- 에디터에서 DLL 로드, `MD.MultiUser.Open` 실행, 연결 전 화면 및 현재 레벨 이름 표시 확인.
- 실제 466 × 726 창 캡처: `Docs/MultiUser/UI/workspace.png` (창 테두리 포함). 한글·줄바꿈·버튼 배치 확인.
- 팀 세션에는 참가하지 않았으므로 다중 참여자 목록, 연결 중 상태, 세션 나가기 저장 대화상자의 실사용 검증은 남아 있습니다. macOS 빌드는 이 Windows 환경에서 실행하지 않았습니다.


## 작업 공간 설정

패널 오른쪽의 **사용자 이름 옆 톱니바퀴**를 누르면 전용 설정창이 열립니다.

- 표시 언어: 한국어 / English. **적용**을 누르면 패널, 설정창, 탭, 메뉴 문구에 반영됩니다.
- 기기와 플랫폼 이름 표시: 참여자별 보조 정보를 표시하거나 숨깁니다.
- 취소 또는 창 닫기: 아직 적용하지 않은 선택을 버립니다. 이미 적용한 값은 유지합니다.
- 프로필·연결 설정 링크: 표시 이름, 아바타 색상 등을 관리하는 Unreal 기본 설정을 엽니다.

개인 설정은 `Saved/Config/MDMultiUser.ini`에 저장됩니다. 파일을 다시 읽어 저장 결과가 일치하는지 확인한 뒤 화면을 갱신합니다. 팀원 설정과 Unreal 기본 브라우저 언어는 바뀌지 않습니다. 영어 모드에서 팀 가이드 링크는 한국어 문서임을 표시합니다.

콘솔: `MD.MultiUser.Settings`로 설정창을 열 수 있고, `MD.MultiUser.Language ko` 또는 `MD.MultiUser.Language en`으로 언어를 적용할 수 있습니다.

### 설정 기능 검증 — 2026-09-15

- 최종 UE 5.8 Win64 Development 전체 에디터 빌드 성공, 재실행 및 플러그인 로드 확인.
- 57개 UI 문구 키에 영어 번역이 빠짐없이 존재함을 검사.
- 실제 UI에서 톱니바퀴 → 드롭다운 → English → 적용: 설정창과 협업 패널 영어 반영 및 `Language=en` 파일 기록 확인.
- 한국어 선택 후 취소: 저장 파일의 영어 유지, 설정창을 다시 열었을 때 English 선택 유지 확인.
- 기기 표시 끄기 → 적용: `ShowDevices=False` 저장 확인.
- 최종 한국어와 기기 표시 켜기를 UI로 적용하고 `Language=ko`, `ShowDevices=True` 독립 확인.
- 실제 화면: `Docs/MultiUser/UI/settings-ko.png`, `settings-en.png`. 팀 세션에는 참가하지 않았습니다.


## 배치 도구 — 2026-09-18

**함께 편집 → 배치 도구 열기** 또는 콘솔 `MD.MultiUser.Placement`로 엽니다. 기존 설정창의 한국어/English 선택을 그대로 따릅니다. 창은 도킹할 수 있고 작은 높이에서는 내용이 스크롤됩니다.

- 빠른 배치: 수직 바닥 스냅(U), 그리드 맞춤(Y).
- 자체 모서리 스냅: 원본 LOD0 메시에서 경계와 25도 이상 꺾이는 모서리를 추출합니다. UV/재질 분리 지점은 용접하고, 평면 내부 삼각형 대각선은 제외하며, 분할된 직선 모서리는 합칩니다. Unreal의 기본 모서리/버텍스 스냅을 호출하지 않습니다.
- 끝점 정렬과 미리보기 이동 거리(1~1,000cm). 회전 없이 평행한 모서리만 후보가 됩니다.
- 가까운 메시 자동 탐색 또는 기준 액터 고정. 최대 12개의 서로 다른 변환 후보를 이전/다음으로 비교합니다.
- 원래 모서리(주황), 기준(청록), 적용 결과 모서리(초록)를 미리보기로 표시합니다. 적용 전에는 이동하거나 저장하지 않으며, 적용은 한 번의 Undo로 되돌릴 수 있습니다.

### 사용 순서

1. 이동할 액터 하나를 선택하고 거리와 맞춤 기준을 설정합니다.
2. 특정 대상에 맞추려면 먼저 대상 액터를 선택해 **선택을 기준으로 지정**한 뒤 이동할 액터를 다시 선택합니다. **주변 자동 탐색**은 기준 지정을 해제합니다.
3. **모서리 후보 미리보기**를 누르고 후보를 비교합니다. 초록 모서리와 면 겹침을 확인한 다음 **적용**합니다.
4. 선택·위치·편집 가능 상태가 바뀌면 미리보기가 취소됩니다. 메시 모서리 변경도 적용 직전에 다시 검사합니다. 이동 취소는 Ctrl+Z입니다.

### 지원 범위

모서리 스냅은 StaticMeshComponent가 루트인 액터 한 개(Blueprint 포함)를 이동합니다. 스켈레탈 메시, ISM/HISM, 루트가 아닌 Blueprint 내부 메시, 부모/자식이 연결된 액터는 지원하지 않습니다. 음수 및 비균일 스케일은 유지하지만 0에 가까운 스케일은 제외합니다. 숨김·액터 이동 잠금·레벨 잠금·다른 Concert 참여자의 리소스 잠금·PIE 상태에서는 이동하지 않습니다.

자동 탐색은 주변 메시 최대 16개, 전체 비교 200만 회로 제한하며 후보가 제한되면 안내합니다. 메시당 원본 삼각형 5만 개, 분할 모서리 2,048개까지 지원합니다. 고정 대상도 거리·비교 제한을 따릅니다. 회전은 모서리 방향을 맞추는 최소 회전이며, 면 전체의 방향이나 물체 충돌까지 해결하지 않습니다. 실제 면 겹침은 적용 전에 확인하세요. 거리·각도·맞춤 옵션은 에디터를 종료할 때까지 패널과 드래그가 공유합니다. 기준 액터 지정은 해당 패널의 미리보기에만 적용됩니다.

바닥·그리드 버튼은 U/Y와 같은 자체 배치 경로를 사용합니다. 모서리 스냅은 별도 기하 알고리즘입니다. 적용은 에디터 트랜잭션을 사용하지만 여러 클라이언트의 동시 적용/잠금 경합은 아직 실사용 검증하지 않았습니다.

### 검증

- UE 5.8 `MarineDemolitionEditor Win64 Development` 전체 빌드 성공.
- `MDMultiUser.Placement.EdgeMath`, `MeshFeatures`, `ApplyUndo`, `HoldDrag` 모두 성공: 끝점·중앙 정렬, 반대 방향 모서리, 거리/각도 제한, 회전, 음수/비균일 스케일, 큐브의 12개 실제 모서리 추출, 미리보기 무변경, 적용, Undo, 오래된 후보 거부.
- 별도 `/Engine/Maps/Entry`에서 실제 UI 미리보기 무변경, 적용 X=0→20cm, End Z=250→50.0001cm, Ctrl+Z 복구, 10도 경사면 정렬 확인. 테스트 액터 제거 후 dirty map/content 0, 팀 세션 연결 없음. 맵은 저장하지 않았습니다.
- 한국어/영어 UI와 언어 변경 시 후보 취소 확인. 최종 사용자 언어는 한국어로 복구했습니다.
- 화면: `Docs/MultiUser/UI/placement-ko.png`, `placement-en.png`. 자세한 검증 요약: `Docs/MultiUser/placement-verification.md`.

자동 테스트는 별도 에디터에서 실행합니다. `ApplyUndo`와 `ClickSnap`은 작업 중인 맵과 Undo 기록을 보호하기 위해 `-MDPlacementTest`가 없으면 실행하지 않고 경고를 남깁니다.

```powershell
& E:/Unreal/git/UE_5.8/Engine/Binaries/Win64/UnrealEditor.exe E:/Unreal/ProjectATM/MarineDemolition.uproject /Engine/Maps/Entry -Unattended -NoSplash -NoSound -MDPlacementTest '-ExecCmds=Automation RunTests MDMultiUser.Placement' '-TestExit=Automation Test Queue Empty' -ReportExportPath=E:/Unreal/ProjectATM/Saved/MDMultiUserReview/PRFinalTests
```


## 현재 사용 가능한 기능: U / B / Y

- **U 한 번**: XY·회전·크기를 유지하며 월드 -Z 방향으로 수직 바닥 스냅합니다. 메시 경계 상자 아래에서 최대 1km 탐색하며 위로 올라가지 않습니다. 바로 아래 충돌 바닥이 없으면 이동하지 않습니다.
- **B를 누른 채 대상 클릭**: 선택한 액터의 실제 메시 모서리 끝점을 클릭한 대상의 가까운 끝점에 맞춥니다. 회전은 유지하므로 평행한 모서리가 필요합니다. 원래 액터 선택을 유지하며 한 번의 Ctrl+Z로 복구합니다.
- **Y 한 번**: 선택 액터 피벗을 현재 에디터 그리드에 맞춥니다.

N(중앙), J(모서리 회전), I(경사면)는 단축키 등록과 실행 경로를 비활성화했습니다. 사용자 저장 단축키가 남아 있어도 해당 기능은 실행되지 않습니다. 배치 패널에서도 중앙·회전 옵션과 선택 보기 버튼을 제거했습니다. 모서리 후보 미리보기는 회전 없는 끝점 맞춤만 제공합니다. 이전 검증 절과 이미지는 과거 기능의 기록입니다.

키 해제/Esc/포커스 상실은 B 대상 지정 대기를 취소합니다. 이미 실행한 이동은 Ctrl+Z로 되돌립니다. 바닥·그리드 버튼은 U/Y와 같은 실행 경로입니다. 엔진 자체의 End/F 기본 단축키는 변경하지 않습니다.

모서리 원본·대상은 StaticMeshComponent가 루트인 액터(Blueprint 포함)를 지원합니다. ISM/HISM, 스켈레탈 메시, 루트가 아닌 내부 메시는 제외합니다. 부착 관계·숨김·편집 잠금·PIE 보호와 메시 복잡도 제한은 유지합니다. B로 명시한 대상에는 패널의 주변 탐색 거리 제한을 적용하지 않습니다.

키 변경: 에디터 개인설정 → 키보드 단축키 → Multi-User Placement. 패널은 설정된 기본 키를 표시합니다. 기존 FEdMode 기반이므로 기본 선택 모드에서 기존 기즈모를 사용합니다. 랜드스케이프/모델링 등 다른 편집 모드에서는 이 기능을 사용하지 않습니다.
