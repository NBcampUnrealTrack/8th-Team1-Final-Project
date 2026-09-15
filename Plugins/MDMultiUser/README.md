# 함께 편집 — Multi-User 작업 공간

`feat/multi-user-editing`의 Concert 설정 및 실행 도구와 함께 사용하는 프로젝트 전용 에디터 패널입니다.

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
