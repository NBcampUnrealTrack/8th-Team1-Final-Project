# 화면 고정 헤라질 개편 Tasks

**문서 상태:** 자동화 구현 완료 — TASK 0~7 구조·자동화 구현 완료(출시 Profile 승인 대기) / TASK 8 구조·자동화 검증 완료(출시 Profile·FirstPerson rig 수동 승인 대기) / TASK 9 자동화 41개 통과(수동·멀티·성능 검증 대기)

**입력 기준:** `RMB Hold = 헤라 Focus(화면 고정)`, `LMB Hold = 기존 긁기 Ability`

**화면 고정 기준:** View 회전·FOV를 고정하고, 카메라 위치는 Pawn의 작은 네트워크/물리 보정만 제한적으로 따라간다. 의도한 이동이나 허용치 이상의 강제 이동은 Focus를 종료한다.

**핵심 목표:** 유효한 헤라 Trace 거리 안에서만 고정 시야에 진입하고, 고정된 화면 안의 가상 조준점으로 실제 크기의 헤라 판정을 끊김 없이 수행한다.

**주요 대상:**

- `Source/MarineDemolition/{Public,Private}/Component/Scrape/MD_ScrapeToolComponent.{h,cpp}`
- `Source/MarineDemolition/{Public,Private}/Ability/Scrape/MD_GA_Scrape.{h,cpp}`
- `Source/MarineDemolition/{Public,Private}/Character/MD_Character.{h,cpp}`
- `Source/MarineDemolition/{Public,Private}/Player/MD_PlayerController.{h,cpp}`
- `Source/MarineDemolition/Public/Data/Input/MD_InputConfig.h`
- `Source/MarineDemolition/Public/Types/Scrape/MD_ScrapeTypes.h`
- `Source/MarineDemolition/{Public,Private}/Gameplay/Scrape/MD_ScrapeEvaluator.{h,cpp}`
- `Content/Blueprint/Input/{IA,IMC,InputConfig}`
- `Content/Blueprint/Character/BP_MDPlayerCharacter.uasset`
- `Content/Assets/Character/Shark/Anim/ABP/ABP_MD_Shark.uasset`
- `Source/MarineDemolition/Private/{UI,World}`의 InputMode 전환 지점
- `Source/MarineDemolition/Private/Tests/Scrape`

---

## 0. 한 줄 요약

헤라를 장착한 상태에서 화면 중앙의 유효 작업 대상이 `TraceDistanceCm` 안에 있을 때만 우클릭 Focus에 진입한다. Focus 중에는 카메라를 돌리지 않고 가상 포인터만 움직이며, 좌클릭으로 그 포인터 경로를 따라 고정 크기의 헤라 접촉면을 적용한다.

---

## 1. 현재 문제와 원인

| 현상 | 현재 구현 | 개편 방향 |
|---|---|---|
| 세로 긁기에서 판정이 넓적해짐 | Evaluator는 이미 고정 크기 footprint를 샘플링하지만, 매 선분의 `StrokeDelta`로 날 방향을 다시 만들며 시각 방향·도구 데이터·표면 투영과 어긋날 수 있음. 정확한 원인은 재현 계측 필요 | 순간 footprint와 sweep 결과를 분리 측정하고, 확인된 원인만 수정 |
| 헤라질이 종종 끊김 | 한 프레임 Trace miss, 표면/면 변경, 축 수직 이탈 시 즉시 스트로크 초기화 | 일시 miss와 실제 종료를 분리하고 제한된 재획득 구간만 연결 |
| 멀미 유발 | 축에 따라 Look 입력을 제한하고 `SetControlRotation()`으로 조준을 자동 보정함 | 카메라는 고정하고 마우스 입력은 가상 포인터에만 누적 |
| 멀티에서 새 조준점을 서버가 모름 | 서버는 자신의 `GetPlayerViewPoint()` 중앙만 Trace함 | 가상 aim 샘플을 전송하고 서버가 거리·가림·대상을 다시 Trace하여 검증 |

현재 `TraceDistanceCm` 기본값은 220cm다. 기존 인수인계 문서에서는 실제 헤라 도달 가능 거리를 약 120~130cm로 추정했으므로, 숫자는 별도 플레이 튜닝이 필요하다. 이번 개편에서는 값을 하드코딩하지 않고 **진입·로컬 조준·서버 검증 모두 같은 유효 Trace 거리**를 사용한다.

---

## 2. 기준 동작과 제품 가정

이 문서는 아래 조작을 기본안으로 삼는다. 사용자 요청에 명시되지 않은 항목은 구현 전 기획 확인 지점이며, 변경되더라도 Trace·판정·네트워크 Tasks의 핵심 구조는 유지한다.

| 항목 | 기본안 |
|---|---|
| Focus 입력 | RMB Hold. 토글 아님 |
| 실제 긁기 | Focus 승인 후 기존 LMB Hold |
| 이동 | deadzone을 넘는 실제 이동 입력 시 Focus 종료 후 이동 |
| 기존 이동 상태 | 수영·사다리 등 진입 허용 여부는 새 규칙을 만들지 않고 기존 `Ability.Scrape` 활성 조건을 그대로 사용 |
| 유효 타깃 이탈 | 짧은 miss는 판정 pause, 지속 이탈은 설정 시간 뒤 Focus 종료 |
| Pending 중 LMB | 입력을 저장하지 않음. Focus 승인 후 새 LMB Started가 있어야 긁기 시작 |

### 2.1 화면 고정의 범위

1. 고정 대상은 플레이어가 보는 **View 회전과 FOV**다. Pawn과 분리된 카메라 월드 위치를 영구 고정하지 않는다.
2. Focus 중 의도한 이동 입력은 Focus를 먼저 종료한 뒤 적용한다.
3. 작은 네트워크 보정이나 물리 보정은 카메라가 Pawn을 따라갈 수 있다. 이때 가상 ray의 원점도 실제 표시 중인 카메라 원점을 사용해 화면과 판정이 어긋나지 않게 한다.
4. 기준 View 원점에서 허용치 이상 이동하거나 외부 시스템이 View 회전/FOV를 강제로 바꿔야 하면 Focus를 종료한다.
5. Focus 중에는 헤라 조준 보정, 작업용 head bob·sway·camera shake처럼 헤라 조작에서 발생하는 카메라 modifier를 적용하지 않는다. 피격·컷신·텔레포트처럼 우선권이 높은 카메라 사건은 Focus를 종료한 뒤 실행한다.

### 2.2 입력 계약

1. 우클릭은 토글이 아니라 **누르고 있는 동안만 유지하는 Focus 입력**이다.
2. 좌클릭은 기존과 같이 실제 헤라질 Ability를 시작·종료한다.
3. 우클릭 없이 좌클릭하면 헤라질을 시작하지 않는다.
4. 좌클릭을 먼저 누른 뒤 우클릭해도 자동으로 헤라질을 시작하지 않는다. 좌클릭을 다시 눌러야 한다.
5. Focus 안에서도 유효하지 않은 지점에서 누르기 시작한 좌클릭은 나중에 유효 지점으로 이동했다고 자동 활성화하지 않는다. 좌클릭을 다시 눌러야 한다.
6. 비헤라 도구의 기존 좌클릭 Ability 동작은 변경하지 않는다.
7. 헤라 여부는 도구 이름이나 메시가 아니라 현재 도구의 `ToolAbilityTag == Ability.Scrape`로 판정한다.

### 2.3 상태 전이

| 현재 상태 | 입력·사건 | 조건 | 다음 상태와 동작 |
|---|---|---|---|
| `Free` | RMB Started | 헤라 장착 + Ability 사용 가능 + 중앙 Trace 유효 | `FocusPending`; 로컬 View를 즉시 고정하고 BeginFocus 요청 |
| `Free` | RMB Started | 거리 밖, 가림, 비헤라, 사망 등 | `Free` 유지; 카메라·Look 입력을 건드리지 않음 |
| `FocusPending` | 서버 승인 | session·기준 View 합의 성공 | `FocusedReady` |
| `FocusPending` | 서버 거부/timeout | 항상 | `Free`; 로컬 Focus Cleanup |
| `FocusPending` | LMB Started | 항상 | 무시하고 buffer하지 않음. 승인 뒤 새 LMB Started를 요구 |
| `FocusedReady` | Mouse2D | 항상 | 카메라 대신 가상 포인터만 이동 |
| `FocusedReady` | LMB Started | 현재 가상 Trace 유효 | `FocusedScraping`; `Ability.Scrape` 시작 |
| `FocusedReady` | LMB Started | 현재 가상 Trace 무효 | 상태 유지; 헤라질 시작 안 함 |
| `FocusedScraping` | LMB Completed/Canceled | 항상 | 긁기만 종료하고 `FocusedReady` 유지 |
| `FocusedScraping` | 일시 Trace miss | RMB와 LMB 유지 | 오염 적용을 잠시 멈추고 재획득 grace 처리 |
| Focus 상태 | RMB Completed/Canceled | 항상 | 긁기 취소 후 `Free`, 정상 Look 즉시 복구 |
| Focus 상태 | 이동·점프·수영/사다리 전환·장비 변경·UI 진입·사망·UnPossess | 항상 | 먼저 공통 Cleanup을 실행한 뒤 해당 행동 처리 |

Focus는 처음 진입할 때 로컬과 서버 모두에서 유효한 중앙 Trace를 요구한다. 로컬 preflight가 성공하면 화면은 즉시 고정되지만 서버 승인 전에는 오염을 적용하지 않는다. 진입 후 포인터가 잠시 표면을 벗어나는 것만으로 카메라를 즉시 풀지는 않으며, 그동안은 판정만 적용하지 않는다. 설정된 지속 이탈 시간, 플레이어 이동 또는 외부 강제 이동으로 기준 View가 유효하지 않게 되면 Focus를 종료한다.

### 2.4 반드시 지켜야 할 불변 조건

1. Focus 중 마우스를 움직여도 View Yaw·Pitch·Roll·FOV가 헤라 조작 때문에 변하지 않는다. 카메라 원점은 §2.1의 제한된 Pawn 보정만 따른다.
2. Focus 중 `Controller->SetControlRotation()`을 사용하는 헤라 조준 보정은 실행되지 않는다.
3. 실제 오염 제거·역오염·따개비 피해는 계속 서버만 적용한다.
4. 한 샘플의 헤라 접촉면은 항상 `BladeWidthCm × BladeDepthCm`다. 프레임 이동량이 접촉면 크기로 바뀌면 안 된다.
5. 긴 경로는 고정 크기 접촉면의 연속 샘플로 처리하며, 한 장의 큰 사각형으로 처리하지 않는다.
6. 모든 종료 경로는 여러 번 호출해도 안전하며 최종적으로 `Focus=false`, `bIsScraping=false`, 정상 Look 상태가 되어야 한다.
7. 판정 크기의 정본은 승인된 도구별 `FMD_ScrapeToolSpec`이다. 메시·소켓·IK가 이 데이터와 일치하도록 검증하고, 불일치를 숨기려고 런타임 판정을 임의 보정하지 않는다.

---

## 3. 구현 Tasks

### TASK 0. 넓적한 판정·끊김 재현과 판정 계약 확정

**우선순위:** P0

**의존성:** 없음

**상태:** 완료 (2026-08-13)

#### 작업

1. 동일한 평면 ScrapeSurface에서 세로·가로·대각·곡선 입력을 같은 거리와 속도로 재현한다.
2. 기본 정사각형 도구뿐 아니라 `BladeWidthCm != BladeDepthCm`인 진단용 ToolSpec으로 아래 값을 기록한다.
   - Raw/Constrained 표면 로컬 시작·끝 위치
   - 프레임별 `StrokeDelta`와 폭·깊이 축
   - 샘플 수·샘플 간격
   - 실제 변경 셀의 X/Y bounds
   - 시각 마스크 bounds와 서버 판정 bounds
3. “순간 헤라 footprint”와 “이동 경로 전체의 sweep union”을 분리해 측정한다. 경로 길이만큼 자국이 길어지는 정상 동작을 footprint가 커진 버그로 오인하지 않는다.
4. 현 Evaluator가 이미 수행하는 고정 크기 샘플링과 최대 간격 세분화를 characterization test로 고정한다.
5. 넓적한 결과의 원인을 아래 후보 중 하나 이상으로 확정한다.
   - 선분마다 바뀌는 날 방향
   - ToolSpec과 실제 날 메시 크기 불일치
   - 표면 로컬 투영·격자 bounds·데칼 시각화
   - Trace miss 뒤 큰 선분 재연결
   - 그 밖의 재현으로 확인된 원인
6. 수정 전 기대 bounds와 §7의 격자 오차를 자동화 테스트 입력/기대값으로 남긴다.

#### 완료 조건

1. 보고된 세로 긁기 현상이 재현되거나, 재현되지 않는다면 현재 코드에서 정상 footprint임을 수치로 입증한다.
2. 넓적한 판정과 끊김 각각의 최초 실패 지점이 입력·Trace·표면 투영·Evaluator·시각화 중 어디인지 확인된다.
3. TASK 5는 이 결과 없이 Evaluator 전체를 재작성하지 않는다.

#### 실행 결과

1. 160×160, 1cm 셀, 24×6cm 진단 헤라, 0.5cm 샘플 간격에서 기준값을 고정했다.
   - 가로·세로 40cm: 변경 1,196셀, 진행축 45cm, 폭축 25cm
   - 대각 40cm: 변경 1,172셀, 진행축 46.669cm, 폭축 24.042cm
   - 0.11cm 단일 표본: 변경 204셀, 진행축 7cm, 폭축 25cm
2. 흔들림 없는 세로 40cm는 1·4·40·80개 선분으로 나눠도 셀별 결과가 동일했다. 총 제거량 누적 오차는 최대 0.000321이었다.
3. X축으로 ±0.75cm 흔들리는 거의 세로인 경로에서는 선분마다 사각 날 방향이 다시 계산되어, 직선 대비 변경 mask가 달라지고 총 제거량이 `57.800875 → 102.675850`으로 증가했다. 따라서 과폭·과제거의 최초 확인 지점은 **Evaluator에 들어간 짧은 선분별 tangent 기반 날 회전**이다.
4. ToolComponent는 세로축 잠금 뒤 옆 흔들림을 2cm까지 허용해 위와 같은 회전 선분을 그대로 만들며, 8cm를 넘는 순간 해당 선분을 폐기하고 새 anchor로 초기화한다. Trace miss도 즉시 이전 위치를 폐기하므로 끊김 원인이 수치로 확인됐다.
5. 회전된 앞·뒷면의 월드↔표면 좌표 왕복 및 판정 셀은 일치했다. CPU mask atlas는 판정 셀 bounds 안에서 G8 양자화로 양쪽 1셀 이내만 줄었고, 별도의 과폭은 없었다.
6. 자동화 테스트 `MarineDemolition.Scrape.Footprint.Characterization`이 성공했다. 결과는 `Saved/Automation/ScrapeFootprintCharacterization5`, 로그는 `Saved/Logs/ScrapeFootprintCharacterization5.log`에 남겼다.

결론적으로 TASK 5에서는 Evaluator 전체를 다시 만드는 대신, 입력 polyline을 arc-length 기준으로 안정화하고 각 sample에 명시적 blade basis를 전달해 짧은 흔들림마다 footprint가 회전하지 않도록 수정한다. TASK 6에서는 한 프레임 miss와 8cm 이탈을 hard reset과 분리한다.

---

### TASK 1. 헤라 전용 Focus 입력과 상태 추가

**우선순위:** P0

**의존성:** 없음

**상태:** 로컬 구현 완료 (2026-08-13) — 입력 에셋화는 런타임 오버레이로 대체

#### 작업

1. `IA_ScrapeFocus`를 생성하고 `IMC_PlayerCharacter`에서 `RightMouseButton`에 연결한다.
2. `UMD_InputConfig`와 `DA_InputConfig`에 `ScrapeFocusAction`을 추가한다.
3. `AMD_Character::SetupPlayerInputComponent`에서 Started, Completed, Canceled를 모두 바인딩한다.
4. Focus 상태의 소유자는 `UMD_ScrapeToolComponent`로 통일한다. 최소 public API는 아래 역할을 제공한다.
   - Focus 진입 시도 및 성공 여부 반환
   - Focus 종료
   - Focus 여부·현재 aim 유효 여부 조회
   - Focus 중 Look delta 전달
5. `Free / FocusPending / FocusedReady / FocusedScraping` 상태와 Focus session generation을 한곳에서 관리하고, Character와 Ability가 서로 별도 bool을 추정하지 않게 한다.
6. RMB 입력은 헤라가 장착된 경우에만 소비한다. 비헤라 상태에서는 카메라와 다른 도구 입력에 영향을 주지 않는다.

#### 완료 조건

1. 비헤라 도구로 RMB를 눌러도 화면이 고정되지 않는다.
2. 헤라 장착 중 RMB Started/Completed/Canceled가 각각 한 번의 진입/종료 흐름으로 귀결되고, Pending 상태에서도 안전하게 취소된다.
3. 같은 프레임에 종료가 중복 호출되어도 Look 또는 Ability가 잠긴 채 남지 않는다.

#### 실행 결과

1. 헤라 장착 중에만 로컬 Enhanced Input 오버레이를 추가해 RMB Started/Completed/Canceled를 연결했다. 현재는 기존 공유 IMC 바이너리를 변경하지 않는 런타임 액션을 사용하며, 향후 리바인딩 UI가 필요하면 `IA_ScrapeFocus` 에셋으로 이관한다.
2. `Free / FocusPending / FocusedReady / FocusedScraping`과 generation을 ScrapeToolComponent가 단일 소유하며, 다른 장비에서는 RMB를 소비하지 않는다.
3. 장비 교체, Pawn 재시작, `UnPossessed`, `EndPlay`에서 매핑과 Focus를 멱등적으로 정리한다.

---

### TASK 2. Focus 진입용 Trace gate와 공통 aim query 분리

**우선순위:** P0

**의존성:** TASK 1

**상태:** 구조 구현 완료 (2026-08-13) — 도구별 출시 Reach/메시 실측 값은 TASK 8~9에서 최종 승인

#### 작업

1. 현재 중앙 시점 전용 `TryGetCurrentSurfaceHit()`을 임의의 `TraceStart + AimDirection`에도 사용할 수 있는 공통 query로 분리한다.
2. 단순 bool 대신 현재 분기 의미를 보존하는 결과 enum/구조체를 사용한다.
   - `SurfaceHit`: ScrapeSurface를 직접 또는 따개비 뒤 재Trace로 찾음
   - `DirectBarnacleOnly`: 따개비는 맞았지만 뒤 ScrapeSurface는 없음
   - `NoTarget` 또는 `Blocked`: 처리 가능한 대상 없음
3. query 결과에는 최소한 아래 정보가 있어야 한다.
   - 위 결과 종류와 판정 유효 여부
   - 작업 표면 또는 직접 맞은 따개비
   - 표면의 앞/뒷면
   - 표면 로컬 2D 위치
   - 월드 접촉 위치와 법선
   - 실제 Trace 거리
4. Focus 진입 시에는 **현재 화면 중앙 ray**로 preflight Trace를 한 번 실행한다.
5. 첫 가림 물체가 일반 오브젝트면 진입을 거부한다. 현재 지원하는 따개비→뒤쪽 ScrapeSurface 재Trace 규칙은 유지한다.
6. 거리 판정은 진입, 가상 포인터 Trace, 서버 재검증에서 같은 도구별 유효 Trace 거리 getter를 사용한다.
7. 현재 220cm를 그대로 승인하지 않는다. 도구별 메시 날 길이, 팔 IK 최대 도달, 카메라 offset을 실제 플레이에서 측정해 §7 기준으로 최종 Trace 거리와 접촉 오차를 확정한다.
8. 기본/광폭/열선 헤라는 `ScrapeProfile` 같은 단일 도구별 정본을 참조하고, Reach·footprint·제거력·따개비 수치를 한 구조에 둔다. ScrapeToolComponent는 장착 시 서버가 확정한 resolved runtime snapshot을 보유하며, Focus gate·로컬 query·서버 query·IK는 모두 `GetEffectiveScrapeReachCm()` 또는 동등한 getter를 사용한다.
9. `FMD_ScrapeSettings`의 공통 기본값은 legacy/fallback으로만 취급하고 제품 도구의 정본으로 사용하지 않는다. Focus gate와 실제 서버 적용이 서로 다른 값을 갖지 않게 한다.

#### 완료 조건

1. 유효 거리 `D` 기준으로 `D-1cm` 대상은 진입할 수 있고 `D+1cm` 대상은 진입할 수 없다.
2. 거리 안이어도 일반 가림 물체 뒤의 표면에는 진입할 수 없다.
3. 진입 실패 시 ControlRotation, 카메라, 가상 포인터, Ability 상태가 전혀 변하지 않는다.
4. 일반 오염 표면, 뒤 Surface가 있는 따개비, 독립 직접 따개비가 각각 구분되어 기존 규칙대로 query된다.
5. 확정된 최대 거리에서 모든 헤라 등급의 날 끝이 접촉점 2cm 이내에 도달하고 과도한 팔 stretch가 없다. 만족하지 못하면 그 거리는 출시 값으로 승인하지 않는다.
6. 도구별 최종 Trace 거리와 그 측정 근거가 이 문서의 §7 수치 확정표에 기록된다.

#### 실행 결과

1. `SurfaceHit / DirectBarnacleOnly / NoTarget(+Blocked)`를 명시적 결과 구조로 분리하고, 표면·면·로컬 2D·월드 접촉점/법선·실제 거리를 한 번에 반환한다.
2. 일반 가림, 뒤 표면이 있는 따개비, 독립 따개비의 기존 3가지 의미를 보존했다. 표면 투영 실패는 진입 불가로 처리한다.
3. `FMD_ScrapeToolSpec`/도구 `ScrapeProfile`에 Reach를 포함하고 진입·로컬 query·실제 Trace가 `GetEffectiveScrapeReachCm()`을 공유한다. 220cm는 호환 기본값일 뿐 출시 승인 값은 아니다.

---

### TASK 3. 고정 View와 화면 내 가상 포인터 구현

**우선순위:** P0

**의존성:** TASK 1, TASK 2

**상태:** 로컬 구현 완료 (2026-08-13) — 수동 카메라 modifier/컨스트레인 aspect 확인은 TASK 9에 포함

#### 작업

1. Focus 진입 성공 순간의 기준 View 원점·고정 회전·FOV를 저장한다.
2. Character/PlayerController의 camera calculation 경로에서 Focus용 고정 회전·FOV를 사용한다. 이를 위해 매 Tick `SetControlRotation()`으로 되돌리는 방식은 사용하지 않는다.
3. 가상 포인터 위치는 해상도 독립적인 viewport normalized 좌표로 관리하고 화면 안전 영역 안으로 clamp한다.
4. Focus 중 `AMD_Character::HandleLookInput`은 `AddControllerYawInput`/`AddControllerPitchInput` 대신 ScrapeToolComponent에 Mouse2D delta를 전달한다.
5. 가상 포인터의 화면 좌표를 **현재 표시 중인 카메라 원점 + 고정 View 회전·FOV**로 deproject하고 TASK 2의 공통 query를 실행한다.
6. 기준 원점과 현재 카메라 원점의 거리를 감시한다. 작은 Pawn 보정은 허용하되 ray 원점도 함께 갱신하고, 데이터화한 이동 허용치를 넘으면 Focus를 종료한다.
7. 외부 카메라 회전/FOV 변경, 컷신, 텔레포트는 Focus 종료를 먼저 요청한다. Focus 중 허용하지 않는 head bob·sway·camera shake modifier는 적용하지 않는다.
8. 포인터 감도와 clamp 영역은 데이터로 조절할 수 있게 한다. 프레임률과 화면 해상도에 따라 이동량이 달라지지 않게 한다.
9. 기존 `ConstrainLookInput()`과 `KeepAimOnScrapePosition()`/`SetControlRotation()` 경로는 새 Focus 중 사용하지 않는다. 사용처가 없어지면 제거하고 관련 설정도 정리한다.
10. Focus 종료 시 누적 마우스 delta를 카메라에 재적용하지 않는다. 저장한 회전으로 되감는 처리도 하지 않는다.
11. 크기가 deadzone을 넘고 실제 이동에 적용될 입력만 Focus를 종료한다. 0 또는 미세한 `MoveAction` Triggered 값으로 Focus를 해제하지 않는다.
12. 유효한 이동 입력은 Focus를 먼저 종료한 다음 정상 적용한다. RMB를 계속 누르고 있어도 자동 재진입하지 않는다.

#### 완료 조건

1. Focus 중 큰 마우스 입력을 반복해도 표시 View 회전과 FOV는 §7 잠금 오차 이내로 유지된다.
2. 작은 Pawn 위치 보정 후에도 레티클·로컬 ray·실제 접촉점이 같은 화면 위치에 남는다.
3. 기준 원점에서 허용치를 넘는 물리 밀림·서버 보정·텔레포트가 발생하면 그 프레임부터 오염 적용을 중단하고 Focus를 종료한다.
4. 화면은 고정되어 있지만 가상 포인터와 접촉점은 화면 안에서 정상 이동한다.
5. RMB 해제 직후 카메라가 튀거나 이전 조준점으로 스냅하지 않고 일반 Look이 이어진다.
6. 해상도 변경 후에도 포인터가 화면 밖으로 사라지지 않는다.

#### 실행 결과

1. Focus 시작 순간의 View 회전·FOV·원점을 저장하고 `CalcCamera()`에서 회전/FOV만 고정했다. 기존 Tick 기반 `SetControlRotation()` 보정은 Focus 중 실행되지 않는다.
2. Look delta는 normalized 포인터에만 누적되고, 현재 viewport aspect·고정 View 기준 ray로 공통 query를 실행한다. Look과 LMB가 같은 프레임에 와도 최신 ray를 사용한다.
3. 5cm 기준 원점 이탈은 오염 적용 전 Focus를 종료하고, 유효한 이동 입력은 0.1 deadzone을 넘을 때만 Focus를 먼저 종료한 뒤 적용한다.

---

### TASK 4. 기존 LMB Ability와 Focus 생명주기 결합

**우선순위:** P0

**의존성:** TASK 1~3

**상태:** 로컬·서버 생명주기 결합·자동화 검증 완료 (2026-08-14) — 실제 입력·UI·사망 경계 수동 검증은 TASK 9 대기

#### 작업

1. 클라이언트는 서버 Focus Ack를 받은 `FocusedReady`에서만 Scrape Ability 활성화를 요청한다. `FocusPending` 중 LMB Started는 무시하고 buffer하지 않으며, 승인 뒤 새 LMB Started를 요구한다.
2. LMB 시작 시 `{FocusSessionId, StartAimSequence, CameraRelativeAim}`을 prediction key와 결합된 replicated TargetData 또는 동등한 GAS activation payload로 전송한다. 첫 접촉을 이전 Unreliable aim이나 서버 화면 중앙에서 추정하지 않는다.
3. 서버 `UMD_GA_Scrape`는 승인된 Focus session, 정확한 헤라 장착, Ability 사용 가능 여부와 첫 aim payload를 먼저 확인한다. 첫 aim의 session·sequence·basis·거리·가림·대상을 검증한 뒤에만 `CommitAbility()`와 권위 `StartScraping(ValidatedAim)`을 실행해 실패 입력에 비용·쿨다운이 소모되지 않게 한다.
4. `StartScraping()`의 첫 프레임 중앙 시점 Trace는 제거하고, prediction key에 결합되어 서버가 검증한 첫 aim을 스트로크 기준점으로 사용한다. `StartAimTimeout` 안에 첫 aim이 없으면 해당 Ability만 취소하고 Focus는 `FocusedReady`로 유지한다.
5. Ability 활성화에는 Focus session id를 명시적으로 결합한다. 서버가 단순히 “현재 최신 Focus”를 추정해 오래된 활성 요청을 새 session에 붙이지 않게 한다.
6. 헤라 입력은 `Ability.Scrape`에 해당하는 활성 가능 spec이 정확히 하나인지 검증하고 그 `FGameplayAbilitySpecHandle`로 `TryActivateAbility()`를 호출한다. 성공한 handle을 press 시 저장하고 release/Cleanup에서 같은 handle만 취소한다. `TryActivateAbilitiesByTag()`가 handle을 반환한다고 가정하거나 tag 전체 취소를 정상 경로로 사용하지 않는다.
7. LMB를 누른 채 슬롯을 바꾸는 경우 아래 순서를 보장한다.
   1. Scrape Ability 취소
   2. Focus 종료 및 스트로크 초기화
   3. 장비 변경
8. 다음 사건은 공통 idempotent Cleanup을 호출한다.
   - RMB Completed/Canceled
   - 이동·점프 입력 및 이동 모드 전환
   - 휠/숫자키 장비 변경
   - Interact/UI 입력 모드 전환
   - 사망
   - 입력 포커스 상실
   - `UnPossessed`, `EndPlay`, 레벨 전환
9. `AMD_PlayerController` 또는 공통 UI 입력 전환 API에 “게임 입력을 잃기 전 활성 Tool Focus 종료” 경계를 만든다. 아래처럼 Character 입력을 거치지 않고 `SetInputMode()`/`DisableInput()`을 호출하는 지점을 모두 이 경계로 이관한다.
   - `UI/Merchant/MD_ShopWidget.cpp`
   - `UI/Merchant/MD_QuestWidget.cpp`
   - `UI/Merchant/MD_MerchantMenuWidget.cpp`
   - `World/Merchant/MD_Merchant.cpp`
   - `World/Interaction/MD_TravelConsole.cpp`
   - `World/Interaction/MD_DayEndConsole.cpp`
   - 각 Travel/DayEnd Widget의 GameOnly 복귀 경로
10. 애플리케이션/viewport 비활성화 callback에서 공통 Cleanup을 호출한다. `IA_ScrapeFocus`의 Canceled만으로 Alt-Tab release 유실을 해결했다고 가정하지 않는다.
11. UI 닫힘, 앱 재활성화, 부활, 장비 재장착 시 이전 Focus를 자동 복구하지 않는다.
12. Ability 활성화가 비용·태그·첫 aim 서버 재검증으로 거부되면 GAS prediction rollback으로 긁기 요청만 취소하고 승인된 Focus는 `FocusedReady`로 유지한다. Focus session 자체가 무효인 거부라면 전체 Focus를 종료한다.

#### 완료 조건

1. RMB 없이 LMB를 눌러도 헤라 Tick과 오염 변경이 시작되지 않는다.
2. Focus 안에서 LMB press/release를 반복하면 Focus는 유지되고 긁기만 시작/종료한다.
3. LMB를 누른 채 RMB를 놓거나 장비를 바꿔도 `bIsScraping`이 남지 않는다.
4. 0/deadzone 이하 이동 입력은 Focus를 해제하지 않고, 실제 이동 입력은 Focus 종료 후 적용된다.
5. 위에 열거한 모든 UI 진입과 Alt-Tab에서 release 이벤트가 유실되어도 카메라·Ability가 잠긴 채 남지 않는다.
6. 사망·UnPossess·레벨 전환 뒤 Focus와 Scrape Tick이 남지 않는다.
7. 서버 승인 전 LMB는 무시되고 승인 뒤 자동 실행되지 않으며, 승인 직후 LMB와 빠른 RMB 재진입에서도 Ability가 다른 Focus session에 붙지 않는다.
8. Focus/aim 검증 실패로 Ability가 시작되지 않으면 비용·쿨다운이 소모되지 않는다.
9. Ability activation과 첫 TargetData의 도착 순서가 바뀌어도 서버 월드 판정은 승인된 첫 aim 전까지 0회이며, 첫 aim timeout은 Focus가 아닌 Ability만 종료한다.

#### 실행 결과

1. Focus Ready와 현재 aim이 유효한 경우에만 정확히 하나의 `Ability.Scrape` spec handle을 활성화하고, release/장비 교체 시 같은 handle만 취소한다.
2. Ability는 Focus/aim을 `CommitAbility()` 전에 검증하며, LMB release는 긁기만 멈추고 RMB Focus는 유지한다. RMB release·이동·점프·상호작용·장비 교체·사망·Pawn 종료는 Ability 취소 후 Focus를 정리한다.
3. 본게임 `AMD_PlayerController::SetInputMode()` 경계와 앱 비활성화 callback에 공통 Cleanup을 연결해 UI/Alt-Tab으로 release가 유실되어도 잠금이 남지 않게 했다.
4. Editor 모듈 빌드가 성공했고 `MarineDemolition.Scrape` 34개 항목을 실행했다. 28개 성공, 기존 자산/픽스처 기대값 문제 2개만 동일하게 실패했고, 이번 변경에서 추가된 헤라 회귀는 없었다. 로그는 `Saved/Logs/ScrapeTask34.log`, 보고서는 `Saved/Automation/ScrapeTask34`에 남겼다.
5. 원격 클라이언트의 서버 GA는 Focus session RPC가 없어 현재 권위 적용을 거부한다. 이 의도된 과도기 제한은 TASK 7에서 Begin/Ack·aim stream·first TargetData를 연결해 제거한다.

---

### TASK 5. 헤라 본래 크기를 보존하는 고정 footprint 판정

**우선순위:** P0

**의존성:** TASK 0, TASK 2~4

**상태:** 판정·경로 구조 구현 완료 (2026-08-13) — Stage3 경로는 TASK 5A, 제품별 Profile/메시·접촉 마커 승인은 TASK 8~9 대기

#### 작업

1. TASK 0의 characterization 결과에서 정상으로 확인된 기존 “최대 간격 세분화 + 고정 크기 샘플” 동작은 보존한다. 실패가 입증된 계층만 수정한다.
2. `FMD_ScrapeStroke` 또는 동등한 입력에 헤라 날의 명시적인 표면 로컬 방향을 추가한다.
   - 예: 정규화된 `BladeWidthAxis`
   - 서버가 검증한 경로에서 계산하고, Evaluator·장애물 판정·애니메이션이 같은 값을 사용한다.
3. 경로는 자유 2D polyline으로 정의하고 **signed 진행 방향 `TravelTangent`**와 footprint·장애물·애니메이션용 **180도 동치 blade 축**을 별도로 관리한다. 별도 회전 입력은 두지 않으며, 깊이축은 안정화된 blade 축, 폭축은 그 수직축을 사용한다.
4. raw Tick/RPC 샘플을 그대로 방향 계산에 쓰지 않는다. 서버가 sequence 순으로 검증한 polyline을 고정 arc-length 간격으로 재샘플하고, smoothing window와 회전 제한은 프레임 수가 아닌 cm와 deg/cm로 데이터화한다. batch 경계에서 방향 상태를 초기화하지 않는다.
5. 방향 임계값 전 샘플은 순서를 보존한 buffer에 누적했다가 첫 안정 방향이 정해지면 원래 polyline으로 replay한다. LMB 종료까지 임계값을 넘지 못하면 제거·역오염 없이 폐기한다. `MinimumTravelDistanceCm` 미만 이동도 previous 위치만 전진시키며 버리지 않고 잔여 arc length를 누적한다.
6. 이후 곡선에서는 blade 축이 경로를 따라 갱신되지만 한 프레임 미세 delta로 떨리지 않게 한다. 왕복 때 `TravelTangent` 부호는 바뀌어도 blade 축은 이전 축과 dot이 0 이상인 동치 방향으로 sign-align하고, 정지·턴어라운드처럼 tangent를 정할 수 없는 구간은 마지막 유효 축을 유지한다.
7. 방향 변화각은 `acos(abs(dot(PreviousAxis, CandidateAxis)))`의 0~90도 값으로 계산한다. 같은 선의 정반대 왕복은 0도로 보되, 설정된 큰 코너는 smoothing 전에 검출해 공통 corner vertex에서 이전 sub-stroke를 닫고 다음 sub-stroke를 시작한다. 두 끝점을 대각 chord로 연결하지 않는다.
8. `FMD_ScrapeEvaluator`는 전달받은 샘플별 위치와 blade basis를 사용한다. 현재 선분의 한 프레임 `StrokeDelta`만으로 방향을 매번 재추정하지 않는다.
   - 폭: `BladeWidthCm`
   - 진행 방향 깊이: `BladeDepthCm`
9. 이전 위치부터 현재 위치까지는 위 고정 사각형 또는 원형 footprint의 **sweep union**으로 평가한다. 두 evaluator 샘플 사이를 이동 간격과 최대 blade 각도 간격 모두 만족하도록 세분화하고, sign-align한 축을 최단각으로 보간한다.
10. 기존 큰 delta 세분화가 새 방향 계약에서도 최대 샘플 간격 이하를 유지하는지 검증한다. 단일 샘플 크기를 늘려 빈 구간을 덮지 않는다.
11. 따개비 장애물 clamp가 현재 `BladeWidthCm / 2` 원형 근사만 사용하는 부분을 새 oriented footprint 기준으로 재검토한다.
12. 기본/광폭/열선 헤라별 `BladeWidthCm`·`BladeDepthCm`과 실제 날 메시·접촉 마커 크기를 대조한다. 의도한 판정 크기를 도구 데이터에 명시하고 공통 fallback이 실수로 모든 등급의 제품 정본이 되지 않게 한다.
13. TASK 0에서 표면 투영·격자 bounds·데칼 시각화가 원인으로 확인되면 해당 계층을 수정한다. 정상 Evaluator 결과를 보정하기 위해 판정 크기를 임의로 줄이지 않는다.

#### 완료 조건

1. 동일 도구로 같은 거리를 세로·가로·대각선으로 움직여도 한 지점의 순간 접촉 크기는 항상 동일하다.
2. 세로 스트로크의 가로 단면과 시작·끝 여유는 §7의 `Rcell` 기반 bounds를 넘지 않는다.
3. 한 프레임 이동 거리가 커져도 개별 footprint의 폭·깊이가 이동 거리로 커지지 않는다.
4. 시각적 헤라 날 방향과 오염 제거·역오염·따개비 충돌 방향이 일치한다.
5. 기획상 정의된 기본/광폭/열선 헤라의 도구별 크기·제거량·적재 배율은 유지된다.
6. 각 헤라 등급의 기대 footprint 수치가 데이터와 자동화 테스트에서 확인된다.
7. 곡선에서 날이 안정 tangent를 따라가고, 왕복에서는 불필요한 180도 메시 flip이 없으며, 큰 코너는 잘못된 대각선 sweep 없이 분리된다.
8. 동일 polyline의 raw 샘플 밀도와 RPC batch 분할을 바꿔도 방향·변경 셀·제거량이 §7 허용 오차 안에서 같다.
9. 매 프레임 이동이 `MinimumTravelDistanceCm`보다 작아도 누적 후 처리되어 30/60/120fps 결과가 같다.

#### 실행 결과

1. `FMD_ScrapeStroke`에 시작/끝 blade basis와 실제 polyline 이동거리를 추가했다. Evaluator는 180도 동치 축을 sign-align하고, 이동 간격과 최대 5도 회전 간격을 모두 만족하도록 footprint를 세분화한다.
2. Component는 자유 2D raw polyline을 0.5cm 고정 arc 간격으로 재표본하고, 2.5cm window와 10deg/cm 회전 제한으로 날 방향을 안정화한다. 1cm 전 입력은 순서대로 보관 후 replay하며 60도 이상 코너는 공통 꼭짓점에서 분리한다.
3. 이동 방향과 blade 방향을 분리해 세로·가로·대각·왕복에서도 `BladeWidthCm × BladeDepthCm`가 이동량에 따라 회전하거나 넓어지지 않게 했다. 원형 도구와 basis 없는 legacy 호출은 기존 계약을 유지한다.
4. 따개비 차단은 비정사각 oriented footprint sweep을 사용한다. 이동 0.5cm·회전 5도 이하로 자체 세분화하고, 기존 원형 반경 API는 호환 경로로 남겼다.
5. 첫 결정성 테스트가 raw tail의 프레임별 즉시 적용 때문에 최대 셀 오차 `0.002521`을 검출했다. 판정 선분도 고정 arc 경계에서만 방출하고 종료 시 남은 tail을 한 번 적용하도록 고친 뒤 30/60/120fps 상당 quarter-circle에서 변경 셀·셀별 잔여량·요청량·총 제거량 차이가 모두 0이 됐다.
6. Editor 전체 빌드가 성공했다. `MarineDemolition.Scrape.Footprint.BladeBasis`, `Footprint.Characterization`, `Barnacle.StrokeBlocking`이 통과했으며 로그는 `Saved/Logs/ScrapeTask5e.log`와 `Saved/Logs/ScrapeTask5d.log`, 보고서는 `Saved/Automation/ScrapeTask5e`와 `Saved/Automation/ScrapeTask5d`에 남겼다.
7. `MarineDemolition.Scrape` 전체 35개를 실행해 33개가 성공했다. 실패 2개는 기존 Structure 자산 개수 fixture와 Hotbar 기본 슬롯 기대값으로 이번 판정 변경과 무관하며, 로그/보고서는 `Saved/Logs/ScrapeTask5Full.log`, `Saved/Automation/ScrapeTask5Full`에 남겼다.
8. 고정 0.5cm 분할 선분마다 원본 Tick의 `DeltaTime`을 다시 사용하면 따개비 충돌 속도가 과소 계산되고 tail은 0이 되는 문제가 리뷰에서 확인됐다. 판정 위치는 TASK 5 결과를 유지하되, 따개비 작업량의 server sample time 계약은 TASK 7에서 authoritative arc/time으로 교체한다.
9. Stage3 역오염은 아직 기존 anchor→furthest 직선 투영을 사용하므로 곡선·왕복 완료로 보지 않는다. 바로 다음 TASK 5A에서 ordered path accumulator로 교체한다.
10. 기본/광폭/열선의 최종 Width·Depth·Reach 및 메시/접촉 마커 승인은 아직 미확정이므로 제품 완료로 표기하지 않는다. TASK 8~9에서 §7.1 표를 확정한다.

---

### TASK 5A. 곡선·왕복 경로에 맞춘 Stage3 역오염 재설계

**우선순위:** P0

**의존성:** TASK 5의 polyline·방향 계약

**상태:** 구현·자동화 검증 완료 (2026-08-13)

#### 작업

1. 현재 `StrokeAnchorPositionCm → FurthestSurfacePositionCm` 단일 직선과 `LockedStrokeAxis`의 단조 전진을 전제로 하는 역오염 보호·tail flush를 polyline 경로 기반으로 교체한다.
2. 기존 단일 직선용 `TryBuildProtectedContaminationPaintStroke()`·`TryBuildPendingContaminationPaintStroke()`를 억지로 재사용하지 않고 ordered path accumulator와 dab 적용 API로 교체한다.
3. 서버 검증 경로 샘플과 pending candidate는 위치, 표면/면, 생성 당시 blade basis, 누적 arc length, sample sequence, tool-spec generation을 보유한다. 누적 거리와 방향은 클라이언트 값을 신뢰하지 않고 서버 승인 위치·sequence에서 계산하며, release 시 현재 tangent로 다시 계산하지 않는다.
4. 제거 판정은 현재 segment에 즉시 적용하고, 역오염은 같은 경로의 **누적 arc length** 기준 고정 간격 bin으로 생성한다. 월드/축 투영값이나 net displacement로 bin을 만들지 않는다.
5. 현재 헤라 접촉면과 겹칠 수 있는 최근 경로는 기존 보호 거리 의미를 유지하도록 pending deque에 남긴다. 보호 거리는 causal arc-length delay이며 영구적인 공간 면역이 아니다. 보호 거리보다 뒤처진 candidate만 제거 처리 뒤 역오염으로 확정한다.
6. 처리 순서는 `(1) 현재 segment 제거 → (2) 누적 거리 증가와 candidate enqueue → (3) 보호 거리만큼 뒤처진 candidate commit → (4) 논리적 sub-stroke 종료 시 잔여 tail 1회 flush`로 고정한다.
7. LMB 종료, hard break, 표면/면 전환, 큰 코너 split에서는 pending tail을 저장된 경로 방향대로 flush한다. reversal 자체는 flush/reset 사유가 아니며, soft miss bridge는 TASK 6 검증에 성공한 경우에만 같은 polyline으로 유지한다.
8. stamp 식별자는 `FocusSession + LMBStrokeGeneration + SubStrokeGeneration + DistanceBin`에서 결정적으로 만든다. 같은 Focus의 LMB 재시작과 hard/corner split마다 generation을 증가시켜 batch 재전송은 중복 적용되지 않되 실제 왕복에는 새 stamp 기회가 생기게 한다.
9. 곡선 각 지점의 역오염 footprint는 candidate 생성 때 저장한 blade basis를 사용한다. 왕복은 실제 이동 거리만큼 누적 arc length가 계속 증가하고, 이미 commit된 역오염 위를 되돌아가면 현재 segment가 정상적으로 다시 제거한다.
10. Stage2→Stage3 전환 시 전환 지점에서 새 accumulator를 시작해 이전 경로에 역오염을 소급하지 않는다. Stage3 종료나 tool spec 변경은 이전 spec generation의 pending tail을 먼저 정리한 뒤 generation을 교체한다.
11. 기존 Stage3의 제거 배율, stamp 양·확률·간격·깊이와 입력 종료 시 tail을 남기는 기획 결과는 유지한다.

#### 완료 조건

1. 직선 경로의 Stage3 결과가 기존 characterization과 §7 오차 안에서 동일하다.
2. 곡선, 90도 코너, U턴, 왕복, soft miss 재획득, hard break 각각에서 역오염이 경로를 벗어난 대각선으로 연결되지 않는다.
3. 동일 polyline을 30/60/120fps와 서로 다른 네트워크 batch로 입력해도 stamp 위치·개수·총량이 §7 오차 안에서 일치한다.
4. LMB/RMB 종료 직전 pending tail이 누락되거나 중복 적용되지 않는다.
5. Stage3에서도 제거 footprint, 역오염 footprint, 헤라 메시 방향이 같은 샘플 방향을 사용한다.
6. `A→B→A` 왕복은 blade axis가 flip/reset되지 않고 누적 거리가 왕복 합만큼 증가하며, return travel은 중복 패킷이 아닌 새 candidate를 만든다.
7. Stroke 도중 Stage3에 진입하면 진입 전 경로에는 역오염 stamp가 생성되지 않는다.

#### 실행 결과

1. Stage3 역오염을 `StrokeAnchorPositionCm → FurthestSurfacePositionCm` 직선 투영에서 ordered path accumulator로 교체했다. 후보는 실제 누적 arc length의 고정 bin에서 만들고 위치·면·생성 당시 blade basis·sample sequence·tool-spec generation·결정적 identity를 저장한다.
2. 서버 권한 처리 순서를 `제거 → candidate enqueue → 보호 거리 뒤 candidate commit → 종료 시 pending tail 1회 flush`로 고정했다. 보호 거리는 기존 removal footprint 반대각선, stamp 깊이, 격자 반대각선 공식을 그대로 사용한다.
3. exact-position dab API와 표면별 bounded identity dedupe를 추가했다. 같은 identity 재전송은 no-op이며, 실제 왕복의 새 distance bin과 다른 플레이어 source는 별도 stamp 기회를 가진다. TASK 7 전 Focus identity는 owner-scoped 64-bit 임시 키이고, 네트워크 세션이 들어오면 서버 권위 session id로 교체한다.
4. 90도 코너는 path segment boundary marker를 통해 이전 segment 적용과 tail flush가 끝난 뒤 generation을 증가시킨다. reversal에는 marker를 만들지 않아 같은 generation과 실제 왕복 arc를 유지한다. 표면·면·hard break·도구 spec 변경·Stage3 이탈은 이전 snapshot tail을 먼저 확정한 뒤 generation을 바꾼다.
5. Stage2→Stage3 전환 segment는 제거만 적용하고 endpoint에서 새 accumulator를 시작한다. 따라서 진입 전 경로에는 stamp를 소급하지 않는다. Stage3 제거 완료 이벤트는 pending dab 확정 뒤 최종 표면 상태로 한 번만 판정하도록 유예·Finalize 경로를 추가했다.
6. `MarineDemolition.Scrape.Load.Stage3OrderedPath`에 곡선 30/60/120fps 상당 분할과 batch, 90도 코너, `A→B→A`, release 중복, Stage2→3, 앞·뒷면, 재전송, 다중 source 충돌 검증을 추가했고 오류·경고 없이 통과했다. 보고서는 `Saved/Automation/ScrapeTask5A`에 남겼다.
7. 기존 `MarineDemolition.Scrape.Load` 3개가 모두 통과했다. 전체 `MarineDemolition.Scrape`는 29개 성공, 5개 경고 성공, 기존 fixture 실패 2개로 TASK 5 이전과 동일하며 보고서는 `Saved/Automation/ScrapeTask5ALoad`, `Saved/Automation/ScrapeTask5AFull`에 남겼다.
8. Editor 전체 빌드가 성공했다. 첫 빌드에서 완료 이벤트 유예 함수의 선언 인자와 Evaluator include 누락을 검출해 수정한 뒤 재빌드했다.

---

### TASK 6. 스트로크 끊김 방지와 안전한 재획득

**우선순위:** P0

**의존성:** TASK 3, TASK 5, TASK 5A

**상태:** 구현·자동화 검증 완료 (2026-08-13)

#### 작업

1. 아래를 서로 다른 상황으로 분리한다.
   - 일시적인 Trace miss
   - 같은 표면·같은 면의 짧은 재획득
   - 다른 표면/다른 면 전환
   - 큰 위치 점프 또는 오래된 네트워크 샘플
   - LMB/RMB 종료
2. `TransientMissGraceSeconds`, `ReconnectCandidateMaxCm`, `ReconnectBridgeMaxCm`, `MaxMissingAimSequences`, `PersistentInvalidExitSeconds`를 데이터화하고 TASK 0 플레이 계측으로 §7의 최종값을 확정한다.
3. grace 안에 같은 SurfaceComponent 인스턴스·같은 면을 `ReconnectCandidateMaxCm` 안에서 재획득하고 누락 sequence가 허용 범위 이내인 경우에만 재연결 후보로 본다. 중간 sweep 샘플도 같은 표면이며 가림이 없음을 서버가 검증한 짧은 gap만 bridge한다. 최대 bridge 길이는 실제 Evaluator sample spacing과 `ReconnectBridgeMaxCm` 중 작은 값이다.
4. 위 bridge 조건을 하나라도 만족하지 못하면 invalid 구간을 칠하지 않고 새 기준점에서 시작한다. “Focus 유지”와 “자국 연결”을 별개로 처리한다.
5. 유효 타깃 없음·거리 초과·가림이 `PersistentInvalidExitSeconds` 이상 지속되면 긁기와 Focus를 종료한다. 대상 파괴나 이동도 동일한 지속 이탈 규칙을 사용하되, 도구 변경·사망·프로토콜 오류는 grace 없이 즉시 종료한다.
6. 기존 `MaximumPerpendicularDriftCm` clamp와 `BreakStrokePerpendicularDistanceCm`에 의한 입력 폐기는 새 자유 포인터 경로에서 제거하거나 legacy 전용으로 격리한다.
7. LMB를 계속 누른 상태에서 transient grace를 넘겼지만 persistent exit 전인 miss 뒤 유효 표면을 찾으면 새 sub-stroke로 안전하게 재개한다.

#### 완료 조건

1. 같은 표면에서 한 프레임 Trace가 빠져도 §7의 bridge 조건 안에서는 Focus가 풀리거나 한 sample 간격보다 큰 자국 공백이 생기지 않는다.
2. 표면 가장자리를 잠시 벗어났다가 bridge 제한 밖에서 돌아오면 Focus는 유지할 수 있지만 빈 공간은 칠하지 않고 새 sub-stroke가 시작된다.
3. 다른 표면으로 이동하거나 큰 점프가 발생하면 두 표면 사이를 긁은 것으로 연결하지 않는다.
4. 30/60/120fps에서 같은 화면 경로의 제거 범위와 제거량이 §7 오차 안에서 일치한다.
5. 지속적인 거리 초과·가림·대상 파괴는 설정 timeout 뒤 Focus를 종료하고 마지막 stale 위치에 판정을 적용하지 않는다.

---

#### 실행 결과

1. Trace miss를 즉시 reset하던 경로를 `TransientMissGraceSeconds=0.10s`와 `PersistentInvalidExitSeconds=0.50s`의 두 단계 lease로 교체했다. grace 동안 Focus와 기존 Stage3 tail은 유지하고, grace 초과 시 old tail만 한 번 확정한 뒤 Focus는 timeout까지 유지한다.
2. 같은 `SurfaceComponent`·같은 면·15cm 이내 재획득만 후보로 분류하며, 실제 bridge는 `min(Evaluator sample spacing, ReconnectBridgeMaxCm)` 이내이고 중간 지점 authority Trace가 같은 표면·면을 가림 없이 다시 맞힐 때만 허용한다. 조건 밖 구간과 valid→valid 15cm 초과 점프는 빈 공간을 칠하지 않고 새 anchor에서 시작한다.
3. 직접 맞은 따개비는 유효 접촉으로 처리해 persistent-invalid 시간을 소비하지 않되 이전 표면 path와는 hard split한다. 다른 표면·면 전환, LMB/RMB 종료 및 기존 도구·사망·UI cleanup도 Stage3 tail을 중복 없이 한 번만 확정한다.
4. Evaluator가 실제 사용하는 sample spacing 계산을 공용 API로 노출해 판정 세분화와 재획득 bridge가 같은 수치를 사용하도록 했다. 네트워크 sequence는 프레임 수로 추정하지 않고 TASK 7의 서버 승인 sequence가 들어올 때까지 unknown으로 취급한다.
5. `MarineDemolition.Scrape.Focus.Reacquisition`에 0.09/0.11초, 14/15/15.01/16cm, bridge 경계·가림·표면/면, sequence 2/3, timeout T±0.01, 30/60/120Hz, Stage3 soft/hard tail exactly-once 검증을 추가했고 오류·경고 없이 통과했다. 보고서는 `Saved/Automation/ScrapeTask6Final`에 남겼다.
6. `MarineDemolition.Scrape.Load.Stage3ContaminationStamp`와 `MarineDemolition.Scrape.Load.Stage3OrderedPath`도 함께 재실행해 2개 모두 통과했다. 보고서는 `Saved/Automation/ScrapeTask6Stage3`에 남겼고 Editor 전체 빌드도 성공했다.

---

### TASK 7. 가상 aim 네트워크 전송과 서버 권한 검증

**우선순위:** P0

**의존성:** TASK 2, TASK 3, TASK 5A, TASK 6의 데이터 계약

**상태:** 네트워크 구조·자동화 검증 완료 (2026-08-14) — §7.1 출시 Profile과 실제 리슨/전용 서버 승인 대기

#### 작업

1. Focus RPC를 아래 역할로 분리한다.
   - `ServerBeginScrapeFocus`: Reliable
   - `ClientScrapeFocusBeginResult(Accepted/Denied)`: Reliable
   - `ServerEndScrapeFocus`: Reliable, 중복 호출 안전
   - `ClientTerminateScrapeFocus`: Reliable, 서버 timeout·자격 상실·프로토콜 위반 통지
   - `ServerUpdateScrapeAim`: Unreliable, sequence 기반
2. 로컬 preflight 성공 시 새 `FocusSessionId`로 `FocusPending`과 예측 화면 고정을 시작하고 Begin을 보낸다. 서버는 session snapshot을 저장한 뒤 Ack를 보내며, 클라이언트는 현재 session의 Ack 뒤에만 `FocusedReady`와 aim stream을 시작한다. Character와 ASC가 다른 actor channel을 써도 Ability 요청이 서버 Focus보다 먼저 실행되지 않게 한다.
3. Begin 요청에는 session id, 초기 sequence, 고정 View 회전·FOV를 포함한다. 서버는 권위 Pawn eye 원점·View와 요청 basis의 §7 허용 오차 및 중앙 preflight Trace를 검증한다. 클라이언트 위치는 권위 원점으로 사용하지 않는다.
4. 승인 시 서버 eye 원점과 합의된 회전·FOV를 session snapshot으로 저장하고 Ack에 동일 basis 또는 digest를 돌려준다. 기준이 허용 오차 밖이면 값을 조용히 보정해 화면을 스냅하지 말고 Begin을 거부한다.
5. Focus 중 world hit를 신뢰하지 않고 합의된 기준 View에 대한 압축 camera-relative aim direction을 보낸다. 연속 샘플은 session id, 단조 증가 sequence, monotonic sample time을 포함하며 §7 rate로 제한한다. 필요하면 최근 샘플을 작은 batch로 중복 전송하되 동일 sequence는 재적용하지 않는다.
6. Ack 뒤 aim은 Ability 활성 여부와 무관하게 `FocusedReady`와 `FocusedScraping` 모두에서 전송·검증한다. 포인터가 정지해도 session lease가 만료되지 않도록 제한된 주기의 heartbeat를 보내며, `NoContact` heartbeat도 lease만 갱신하고 월드 판정을 만들지 않는다.
7. LMB 시작의 첫 aim은 TASK 4의 prediction-key-bound TargetData로 신뢰성 있게 전달한다. 서버는 이전 Unreliable 샘플이나 중앙 ray를 첫 기준점으로 대신 사용하지 않고, 검증된 첫 aim 전에는 `CommitAbility()` 또는 권위 월드 판정을 실행하지 않는다.
8. 서버는 매 샘플 시점의 권위 Pawn eye 원점과 합의된 고정 회전·FOV로 world ray를 복원해 TASK 2 query를 다시 수행한다. session 시작 원점에서 Pawn이 이동 허용치를 넘거나 실제 View basis가 허용치를 벗어나면 Focus를 종료한다.
9. 서버 검증은 다음 세 계층으로 분리한다.
   - Focus/session: session id, sequence, basis, finite/normalized 값, view cone, sample age, 헤라 장착
   - 접촉: 권위 eye 원점, `TraceDistanceCm`, 가림, ScrapeSurface/따개비, 각속도·표면 이동 거리
   - 판정 적용: 같은 session의 `Ability.Scrape` 활성, 첫 aim 승인, sample freshness
10. 유효하게 인코딩된 ray가 거리 밖·가림·비대상을 가리키면 프로토콜 오류가 아니라 `NoContact`다. Focus와 활성 Ability는 유지하되 적용만 중단하고 TASK 6의 miss 시간을 갱신한다.
11. 중복·과거 sequence와 다른 session 샘플은 조용히 폐기하고 lease도 갱신하지 않는다. 큰 sequence/time gap, 속도 위반, 너무 오래된 샘플은 판정을 적용하지 않고 스트로크 기준점을 초기화한다. NaN, view cone 위조, 장비/session 불일치 같은 중대한 오류는 session을 종료한다.
12. 마지막 구조적으로 유효한 aim/heartbeat 뒤 `AimSilenceTimeout`이 지나면 서버가 session을 먼저 적용 불가로 표시하고 Ability 취소, 접촉·경로 초기화, Reliable 종료 통지 순으로 원자적 Cleanup한다. 이후 늦은 aim·Ack·Ability cancel·End는 현재 session id가 아니면 무해해야 한다.
13. Begin Deny/Ack timeout은 `FocusPending → Free`, 첫 aim timeout이나 GAS Commit 실패는 `FocusedScraping → FocusedReady`, 개별 `NoContact`는 현재 Focus 상태 유지로 구분한다. 도구 변경·사망·이동 한계·protocol 위반·session timeout만 전체 Focus를 끝낸다.
14. 오염 제거, 적재 증가, 역오염, 따개비 피해는 위 세 계층을 통과한 서버 경로에서만 실행한다. 따개비 속도는 서버 검증 연속 샘플 거리와 clamp된 sample time으로 계산하며 RPC 도착 간격을 타격 속도로 사용하지 않는다.
15. 서버 확정 접촉 위치·blade 방향·유효 플래그는 기존 결과 복제 경로와 결합해 소유 클라이언트 피드백과 원격 애니메이션에 전달한다. 위치만 복제하고 원격 클라이언트가 방향을 재추정하게 두지 않는다.

#### 완료 조건

1. 리슨 서버 호스트·원격 클라이언트·전용 서버 클라이언트 모두 가상 포인터와 서버 확정 제거 위치가 §7 오차 안에서 일치한다.
2. Begin 성공 시 Ack 뒤에만 `FocusPending → FocusedReady`가 되고, Pending LMB는 GA를 시작하거나 승인 후 자동 실행되지 않는다.
3. Pending 중 RMB 해제/Ack timeout 뒤 늦은 Ack가 Focus를 부활시키지 않으며, 빠른 재진입의 이전 session 패킷이 새 session에 적용되지 않는다.
4. 서버 Focus 없는 GA, 잘못된 첫 aim, TargetData timeout, Commit 실패는 첫 판정 전 거부된다. Ability activation과 TargetData의 도착 순서가 바뀌어도 같은 prediction key의 승인된 첫 aim 전 월드 변경은 0회다.
5. Ready aim은 Ability가 비활성이어도 접촉 상태를 갱신하지만 오염·적재·따개비 피해는 변경하지 않는다. Ability가 활성이어도 첫 aim 승인 전에는 변경하지 않는다.
6. 거리 밖·가림·비대상 ray는 `NoContact`가 되어 판정만 멈추고, NaN·비정규화·cone 밖·잘못된 session은 정의된 정책대로 폐기 또는 종료한다.
7. 단일 aim 유실은 §7 범위에서 이어지고 큰 sequence/time gap 뒤에는 긴 직선이 생기지 않는다. 중복·역순 sequence와 heartbeat는 제거량을 늘리지 않는다.
8. aim silence timeout은 서버 Ability와 Focus를 모두 종료하고 소유 클라이언트 화면을 해제한다. LMB/RMB release·timeout·장비 변경이 같은 프레임에 겹쳐도 최종 상태는 `Free`, GA inactive, `bIsScraping=false`다.
9. 허용된 작은 Pawn 위치 보정 뒤에도 클라이언트 화면 ray와 서버 재구성 ray가 §7 오차 안에서 일치한다.
10. §7의 지연·유실 환경에서 Begin/Ack, 첫 TargetData, Unreliable aim 순서가 달라도 첫 판정 위치와 제거 결과가 동일하다.

#### 실행 결과

1. Focus session을 Reliable Begin/Ack/End/Terminate와 sequence 기반 Unreliable aim으로 분리했다. Ready는 5Hz heartbeat, Scraping은 30Hz로 제한하고 서버에도 30Hz token bucket과 burst 상한을 적용했다.
2. LMB 첫 aim은 prediction key에 묶인 TargetData로 전달하며, 서버가 이를 검증하기 전에는 Commit과 권위 월드 변경을 막는다. actor channel 재정렬 때 먼저 도착한 연속 샘플은 최대 8개를 보관해 첫 aim 뒤 sequence 순으로 재생하고 불연속은 old anchor를 끊는다.
3. 서버는 클라이언트 hit를 사용하지 않고 권위 eye 원점과 합의된 View basis로 거리·cone·가림·표면/따개비를 재Trace한다. 중복·과거·다른 session은 lease도 갱신하지 않으며, 비단조 시간·큰 sequence/time/position gap은 스트로크만 reset한다.
4. 0.50초 silence, 장비·View 자격 상실, malformed protocol은 session id에 묶인 정확한 GA handle과 Focus를 원자적으로 종료한다. 동일 accepted session Begin 재전송은 상태·시계·token·buffer·접촉을 초기화하지 않는 멱등 Ack로 처리한다.
5. Stage3 identity를 권위 session과 결합하고, 따개비 속도는 승인된 sample time을 fixed-arc 구간에 거리 비례 배분해 RPC 도착 지연이나 Tick 분할에 의존하지 않게 했다. 서버 확정 접촉 위치와 blade 방향도 함께 복제한다.
6. `MarineDemolition.Scrape.Network.Session`에 session/sequence, cone·aspect, first-aim 재정렬, rate limit, age·time gap, heartbeat, stale cleanup, Stage3 identity, 동일 Begin, malformed TargetData 검증을 추가했다.
7. 클라이언트가 임의의 viewport aspect를 선언해 서버 조준 cone을 넓힐 수 없도록 서버 권한 cone의 상한을 고정 16:9 정책으로 분리했다. 클라이언트의 실제 projection은 레티클과 ray 정합에만 유지하고, 더 넓은 화면에서는 normalized pointer 범위를 권한 cone 안으로 줄인다. 32:9 위조 ray 거부를 포함한 최종 `Network.Session` 실행은 성공 1, 경고 0, 오류 0이며 보고서는 `Saved/Automation/ScrapeTask9NetworkAuthorityFinal`에 남겼다.
8. UHT를 포함한 `MarineDemolitionEditor Win64 Development` 전체 빌드가 최종 보완 후 20 actions로 성공했다.

---

### TASK 8. 헤라 애니메이션과 Focus UI 교체

**우선순위:** P1

**의존성:** TASK 3, TASK 5, TASK 7

**상태:** 구조 구현·자동화 검증 완료 (2026-08-14) — §7.1 출시 Profile과 FirstPerson 팔/도구 rig 수동 승인은 TASK 9 대기

#### 작업

1. `BP_MDPlayerCharacter`의 카메라 중앙 220cm 중복 Trace를 제거하고 ScrapeToolComponent의 현재 가상 접촉점을 사용한다.
2. ControlRotation 변화량으로 가로/세로를 추정하는 `UpdateScrapeBladeRoll` 우회 로직을 제거한다.
3. TASK 5에서 확정한 명시적 blade 방향을 `ABP_MD_Shark`에 전달해 메시와 판정 방향을 일치시킨다.
4. Focus 중 가상 포인터 또는 전용 레티클을 표시한다.
   - 유효 작업 대상: 활성 색
   - 거리 밖/가림/비대상: 비활성 색
   - 서버 거부 또는 Focus 종료: 즉시 숨김
5. 원격 캐릭터는 서버가 확정한 접촉 위치·방향을 사용하고 로컬 카메라 정보를 재추정하지 않는다.

#### 완료 조건

1. 로컬 헤라 날 끝이 가상 포인터의 실제 접촉점과 일치한다.
2. 세로·가로·대각 스트로크에서 메시 방향과 판정 footprint가 일치한다.
3. 다른 플레이어의 헤라가 마지막 로컬 카메라 위치나 화면 중앙을 향해 붙지 않는다.
4. Focus 진입 실패나 강제 종료 뒤 레티클이 남지 않는다.
5. §7에서 확정한 최대 Trace 거리·시야 cone 전체에서 팔/도구 IK가 한계를 넘지 않고 날 끝이 접촉점 2cm 이내에 위치한다.
6. 화면 비율이나 해상도가 바뀐 뒤 동일 normalized screen coordinate의 레티클과 deproject ray 접촉점이 §7 ray 오차 안에서 일치한다.

#### 실행 결과

1. `BP_MDPlayerCharacter` 헤라 애니메이션 함수에서 화면 중앙 220cm 중복 Trace와 ControlRotation 변화량 기반 roll 추정을 제거했다. 두 함수는 native helper로 축소되었고, 로컬은 현재 Focus 접촉점을, 원격 simulated proxy는 서버가 복제한 확정 접촉점·blade 방향과 실제 `bIsScraping` 상태를 사용한다. 따라서 RMB FocusReady만으로 원격 긁기 자세가 켜지지 않는다.
2. 도구 회전은 복제된 blade 방향을 `ThirdPersonToolMesh` 실제 attach parent/socket 공간으로 변환한 뒤, 기존 `AttachOffset`을 보존하는 도구 긴 축 twist만 적용한다. 따라서 전체 yaw/pitch 덮어쓰기로 손잡이 자세가 깨지지 않는다.
3. HUD 자산과 독립적인 native Focus 레티클을 owning local-player screen에 추가했다. `FocusPending`은 대기색, 승인 후 유효 aim은 활성색, 무효 aim은 비활성색으로 표시하고 `Free`가 된 첫 paint부터 숨긴다. `HitTestInvisible`, non-focusable, volatile로 고정해 입력을 소비하지 않는다.
4. Focus 진입 전후의 표시 FOV·projection policy는 바꾸지 않는다. 실제 CameraCache, LocalPlayer axis, reference aspect로 수평/수직 half-tangent를 계산해 로컬 레티클과 ray의 정합을 유지한다. 서버는 요청 projection 계약을 재검증하되 권한 cone은 고정 16:9 정책 이하로 제한하고, 클라이언트는 Ack된 권한 범위에 맞춰 pointer 이동 범위만 줄인다. MaintainY 4:3·16:9·21:9와 MaintainX 계약을 검증했다.
5. 같은 화면 비율의 해상도·DPI 변경은 유지한다. 비율이 합의값 대비 0.0045를 넘거나 projection axis/reference aspect가 바뀌면 다른 ray를 전송하기 전에 Focus·Ability를 공통 Cleanup한다. constrained aspect, orthographic, off-center, 비정상 FOV/aspect는 진입을 거부한다.
6. 최종 `MarineDemolitionEditor Win64 Development` UHT/C++ 빌드가 성공했다. `UI.FocusReticleContract`, `Network.Session`, `Editor.MigrateAnimationPose`, `Editor.BlueprintGraphInspection`은 각각 성공 1, 경고 0, 오류 0이며 보고서는 `Saved/Automation/ScrapeTask8FocusReticleFinal2`, `ScrapeTask8NetworkFinal`, `ScrapeTask8BlueprintFreshCompileFinal`, `ScrapeTask8BlueprintFreshInspectFinal`에 남겼다.
7. 계측 자동화로 현재 세 제품 DA가 모두 `Reach=220cm`, `BladeWidth=12cm`, `BladeDepth=12cm`, Rectangle fallback을 사용함을 확인했다. 현재 메시·AttachOffset과 출시 Profile은 일치한다고 승인할 수 없으며 실측값은 §7.1에 기록했다. `Saved/Automation/ScrapeTask8ToolProfilesFinal`은 계측 자료이지 출시 승인 결과가 아니다.
8. FirstPerson mesh는 현재 AnimClass가 없고 카메라 부착 공간을 사용하므로 ThirdPerson 회전을 그대로 복사하지 않았다. 출시 Reach/cone에서 날 끝 2cm 오차와 팔 stretch를 맞추려면 FirstPerson 전용 rig/IK와 접촉 마커 승인이 필요하다.
9. 현재 프로젝트에는 split-screen 사용처가 없다. 향후 split-screen을 도입하면 전체 viewport가 아닌 LocalPlayer sub-rect를 projection aspect 정본으로 사용해야 한다.

---

### TASK 9. 자동화 테스트와 플레이 검증

**우선순위:** P0

**의존성:** TASK 1~8과 병행하며 TASK 5A 포함

**상태:** 자동화 구현·전체 회귀 통과 (2026-08-14) — 수동 플레이·리슨/전용 서버·패킷 열화·성능·출시 Profile 승인 대기

#### 자동화 테스트

1. Focus 상태 전이와 중복 Cleanup 테스트를 추가한다.
2. 거리 경계, 일반 가림, 따개비 가림 query 테스트를 추가한다.
3. TASK 0 characterization과 고정 footprint 회귀 테스트를 추가한다.
   - 세로/가로/대각선 단면 비교
   - 큰 delta 세분화
   - `BladeWidthCm != BladeDepthCm`인 비정사각형 도구
   - 사각형/원형 도구
   - 앞/뒷면, 회전된 표면, arbitrary mesh, surface seam
4. 한 프레임 miss, grace 내 재획득, grace 초과, 표면/면 전환 테스트를 추가한다.
5. 고정 View session의 Begin 검증, session/sequence 거부, 작은 원점 보정, 이동 한계 초과 테스트를 추가한다.
6. 기존 `MD_ScrapeAimCorrectionTests.cpp`는 제거되는 카메라 보정 테스트 대신 “Focus aim이 표시 View 회전을 바꾸지 않음” 검증으로 교체한다.
7. UI 입력모드 전환과 viewport 비활성화가 공통 Cleanup을 호출하는 통합 테스트를 가능한 범위에서 추가한다.
8. Stage3 직선·곡선·90도·왕복·분할 입력·tail flush의 결정적 stamp 테스트를 추가한다.
9. 따개비 회귀 테스트를 추가한다.
   - 직접 겨냥한 따개비는 뒤 ScrapeSurface 유무와 관계없이 유효 대상으로 처리
   - 일반 물체가 먼저 가리면 Focus 거부, 따개비 뒤 Surface 재Trace는 유지
   - 반대 면·차단 반경 밖은 통과, 내부에서 바깥 이동은 허용, 안쪽 침투는 oriented footprint로 clamp
   - 여러 따개비 중 최초 충돌 대상, unregister/destroy 뒤 유령 차단 없음
   - 제거 직후 뒤 Surface 재획득과 첫 샘플 속도 0 재시작
   - batch/지연/FPS별 타격 속도와 §7 현행 작업량 계약 유지
10. 도구 spec runtime 변경과 빠른 Focus 종료→재진입의 stale packet 테스트를 추가한다.
11. Focus Begin/Ack, GAS activation, 첫 TargetData의 도착 순서와 timeout을 바꾼 테스트 및 Ready/NoContact heartbeat·aim silence 테스트를 추가한다.
12. §7의 격자 bounds·FPS 결정성·100~200ms 지연/유실·4인 RPC/성능 예산을 자동화 또는 측정 가능한 테스트로 고정한다.
13. 기존 `MarineDemolition.Scrape` 자동화 테스트 전체를 실행해 오염 제거·적재·역오염·따개비·복제 회귀가 없는지 확인한다.

#### 수동 플레이 검증

| 구분 | 시나리오 | 기대 결과 |
|---|---|---|
| 진입 | 비헤라 / 거리 밖 / 일반 물체에 가림 | 화면 고정 안 됨, Look 정상 |
| 진입 | 헤라 + 거리 안 유효 표면 | RMB를 누른 동안만 화면 고정 |
| 카메라 | Focus 중 마우스를 화면 끝까지 이동 | 카메라는 고정, 포인터만 이동 |
| 카메라 보정 | 작은 서버 위치 보정 / 허용치 초과 밀림·텔레포트 | 작은 보정은 화면-ray 일치, 큰 이동은 즉시 Focus 종료 |
| 판정 | 세로·가로·대각·곡선·빠른 왕복 | 본래 도구 크기의 연속 자국 |
| Stage3 | 곡선·90도·왕복 후 입력 종료 | 경로 기반 역오염, 대각선 연결·tail 누락·중복 없음 |
| 연속성 | 표면 가장자리와 1~2 프레임 miss | §7 grace·거리·sequence·bridge 조건에 맞춰 유지 또는 새 sub-stroke |
| 안전 종료 | LMB 중 RMB 해제 / 이동 / 점프 / 장비 교체 | 먼저 긁기·Focus 종료 후 행동 수행 |
| UI·상태 | Interact UI / Alt-Tab / 사망 / 부활 | 잠긴 입력 없음, 자동 재진입 없음 |
| 네트워크 | 리슨 서버 2인 / 전용 서버 2인 | 양쪽 제거 위치와 원격 애니메이션 일치 |
| 네트워크 이상 | §7 지연·유실·비정상 aim | p95/최대 접촉 오차 충족, 긴 오염 제거 선 또는 거리 우회 없음 |
| 따개비 | 독립/표면 차단/제거 직후 재획득 | 서버 지연과 무관한 제한 속도 피해, 표면 경로 정상 복구 |
| 프레임률 | 30/60/120fps 동일 경로 | 제거 범위·제거량이 §7 오차 안에서 동일 |

#### 완료 조건

1. 신규 자동화 테스트와 기존 `MarineDemolition.Scrape` 전체가 통과한다.
2. 수동 플레이 검증 표를 로컬·리슨 서버·전용 서버 환경에서 완료한다.
3. 실패한 항목은 재현 절차와 기대 결과를 남기고 해결 전까지 개편 완료로 처리하지 않는다.

#### 실행 결과

1. 제거된 카메라 자동 보정 수학을 검증하던 `AimCorrection.ProportionalCorrection`을 `Focus.CameraLock`으로 교체했다. Free에서는 Look을 소비하지 않고, Focus에서는 normalized 포인터만 움직이며, 표시 View 회전·FOV가 진입 값으로 고정되고 종료 후 정상 View를 덮어쓰지 않는 계약을 고정했다.
2. footprint/basis, Stage3 ordered path, grace 재획득, oriented 따개비 clamp, network session, projection/cone, Focus UI, Blueprint graph, 복제 codec, 앞·뒷면·회전 표면 회귀를 포함한 `MarineDemolition.Scrape` 41개를 전체 실행했다.
3. 서버 권한 cone과 원격 pose 보완 후 다시 실행한 최종 결과는 clean 성공 34, fixture 경고 포함 성공 7, 실패 0, 미실행 0, 프로세스 exit code 0이다. 보고서는 `Saved/Automation/ScrapeTask9FullAuthorityFinal`, 로그는 `Saved/Logs/ScrapeTask9FullAuthorityFinal.log`에 남겼다.
4. 최초 전체 실행에서는 현재 19개인 Structure 자산을 20개로 가정한 검사와, 현재 4칸인 핫바를 1칸으로 가정한 검사 두 건만 실패했다. 제품 동작은 바꾸지 않고 테스트 fixture를 현재 정본에 맞춘 후 전체 통과했다.
5. 성공 7개의 경고는 transient 테스트 mesh의 material/socket 누락, 의도적 invalid-basis 입력, ToolProfile 계측 fixture의 ABP divide-by-zero 로그다. 어떤 항목도 assertion 오류나 제품 실패로 판정되지 않았다.
6. 헤드리스 자동화로 대체할 수 없는 남은 승인 게이트는 다음과 같다.
   - 로컬 실제 입력의 RMB/LMB 순서, 시야 잠금, 해제 스냅, UI·Alt-Tab·사망 Cleanup, 멀미 여부
   - 기본/광폭/열선 출시 Profile, FirstPerson 전용 rig/IK, 날 끝 접촉 오차 2cm, 팔 stretch
   - 리슨 서버 2인·전용 서버 2인에서 제거 위치·원격 pose·원자적 Cleanup 일치
   - 100~200ms RTT, 2~5% loss, 4인 동시 긁기, Insights GT p95/p99·대역폭 게이트

---

## 4. 구현 순서와 의존성

1. TASK 0 — 현상 재현·계측과 판정 계약 확정
2. TASK 1~2 — Focus 입력과 공통 Trace 계약
3. TASK 3~4 — 로컬 고정 화면·가상 포인터·Ability/UI 생명주기
4. TASK 5 — 확인된 footprint 원인과 방향·polyline 수정
5. TASK 5A — Stage3 경로 기반 역오염 전환
6. TASK 6 — 스트로크 재획득·연속성
7. TASK 7 — 서버 Focus session 검증과 aim 전송
8. TASK 8 — 애니메이션·레티클 마감
9. TASK 9 — 각 단계 자동화 테스트 추가 후 전체 회귀 검증

TASK 0의 계측 결과와 §7의 도구 Profile 승인 뒤 TASK 5의 스트로크/방향 구조를 확정한다. TASK 5A의 generation·stamp identity와 TASK 6의 miss 정책은 TASK 7의 네트워크 형식에 포함되므로 서버 전송 구현 전에 먼저 확정한다.

---

## 5. 비범위

이번 개편에서 아래 규칙의 수치나 기획 자체는 변경하지 않는다.

- 헤라 오염 적재 0~100 및 단계별 메시
- 포화 헤라 제거 배율·역오염 확률
- 기본/광폭/열선 헤라의 제거 성능과 적재 배율
- 따개비 내구도와 타격 작업량
- 오염 표면 저장·복제 형식
- 다른 도구의 좌클릭 Ability 입력

`TraceDistanceCm`의 최종 승인, 도구별 footprint 정본 확정, 메시·접촉 마커·IK 정합 검증은 이번 개편 범위다. 다만 제거력·적재·따개비 밸런스 수치 자체는 변경하지 않고 현재 값을 Profile에 옮겨 회귀 기준으로 고정한다. §7의 미확정 값을 남긴 채 구현 완료로 처리하지 않는다.

---

## 6. 전체 완료 정의

아래 항목을 모두 만족해야 개편 완료로 본다.

1. 유효 Trace 거리 안에서만 RMB Focus가 시작된다.
2. Focus 중 카메라 자동 회전이 없고 RMB 해제 시 스냅도 없다.
3. 고정 화면 안의 가상 포인터로 LMB 헤라질을 수행할 수 있다.
4. 세로·가로·대각선에서 도구 본래 footprint가 유지된다.
5. 일시 Trace miss와 낮은 프레임률 때문에 자국이 불필요하게 끊기지 않는다.
6. 큰 miss·표면 전환·패킷 누락을 긴 선분으로 잘못 연결하지 않는다.
7. 이동·장비 변경·UI·사망·입력 취소 뒤 Focus 또는 Scrape Tick이 남지 않는다.
8. 서버가 모든 거리·가림·대상 검증을 수행하고 클라이언트 hit를 신뢰하지 않는다.
9. 리슨 서버와 전용 서버에서 로컬 판정·서버 결과·원격 애니메이션이 일치한다.
10. 기존 `MarineDemolition.Scrape` 자동화 테스트와 신규 테스트가 모두 통과한다.
11. 모든 헤라 등급의 승인된 Reach·footprint·접촉 마커 값이 단일 Profile 정본에 기록된다.
12. §7의 공간·프레임률·네트워크·성능 게이트를 모두 통과한다.

---

## 7. 구현 전 확정할 수치와 측정 기준

아래에서 `[초기값]`은 첫 구현·플레이테스트용 제안값이다. TASK 0과 실제 멀티플레이 측정으로 조정할 수 있지만, 최종값·변경 근거·경계 테스트를 이 표에 기록하기 전에는 해당 Task를 완료 처리하지 않는다. `[검증식]`은 튜닝으로 완화하지 않는 완료 기준이다.

### 7.1 도구별 Scrape Profile 승인표

| 등급 | ReachCm | BrushShape | BladeWidthCm | BladeDepthCm | 제거·적재·따개비 수치 | 메시/접촉 마커 검증 | 상태 |
|---|---:|---|---:|---:|---|---|---|
| 기본 | runtime 220 / FP camera→tip 193.882 / 120~130 후보 승인 대기 | Rectangle fallback | Profile 12 / FP 투영 66.386 / TP 투영 35.820 | Profile 12 / FP 투영 19.811 / TP 투영 10.690 | 현행 값 이관 | 실측 완료, 접촉 마커·rig 수동 승인 대기 | 미확정 |
| 광폭 | runtime 220 / FP camera→tip 112.747 / TP 67.725 | Rectangle fallback | Profile 12 / FP 투영 35.775 / TP 투영 19.303 | Profile 12 / FP 투영 10.676 / TP 투영 5.761 | 현행 값 이관 | 실측 완료, 등급 의도·rig 수동 승인 대기 | 미확정 |
| 열선 | runtime 220 / FP camera→tip 112.747 / TP 67.725 | Rectangle fallback | Profile 12 / FP 투영 35.775 / TP 투영 19.303 | Profile 12 / FP 투영 10.676 / TP 투영 5.761 | 현행 값 이관 | 실측 완료, 열선 시각·rig 수동 승인 대기 | 미확정 |

1. 현재 220cm Trace는 자동 승인하지 않는다. 각 후보 거리 `D`에서 `D-1cm`는 날 끝과 접촉점 오차 2cm 이내이고 과도한 팔 stretch가 없어야 하며, `D+1cm`는 Focus를 거부해야 한다.
2. 판정 정본은 메시 bounds가 아니라 승인된 Profile이다. 다만 `Blade_Left/Right`, `Blade_Tip/Back` 같은 접촉 마커 또는 동등한 기준으로 메시가 Profile과 맞는지 에디터 검증한다.
3. `BladeWidthCm != BladeDepthCm`인 24×6cm 진단 Profile을 제품값과 별도로 두어 0/45/90도 판정·회전·따개비 clamp 회귀에 사용한다.
4. 위 세 행 중 미확정 값이 하나라도 있으면 TASK 2·5·7·8은 완료할 수 없다.

### 7.2 공간 판정 오차

표면 크기를 `Sx, Sy`, 격자를 `Gx, Gy`라 할 때 `CellX=Sx/Gx`, `CellY=Sy/Gy`, `Rcell=0.5×sqrt(CellX²+CellY²)`로 둔다. 현재 자동 격자의 셀 한 변 상한은 2.5cm이므로 정사각 최악 조건의 `2×Rcell`은 약 3.54cm다.

| 항목 | 완료 기준 |
|---|---|
| 변경 셀 외곽 | 변경된 셀 중심이 연속 sweep 경계의 `Rcell + 0.01cm` 바깥에 없어야 함 |
| 스트로크 단면 | 폭축 투영 길이 `≤ BladeWidthCm + 2×Rcell + 0.01cm` |
| 시작·끝 여유 | 경로 앞뒤 변경 셀 중심이 `BladeDepthCm/2 + Rcell + 0.01cm`를 넘지 않음 |
| 내부 누락 | 연속 sweep를 `Rcell`만큼 안쪽으로 줄인 영역의 유효 셀은 모두 변경됨 |
| 분할 독립성 | 같은 polyline을 다르게 분할해도 셀별 잔여량 차이 `≤ 1e-4` |
| 기본 오염 mask | 복제 정착 뒤 정규화 오차 `≤ 1/255`; 충분한 제거 fixture의 변경 texel bounds는 변경 셀 bounds와 0 texel 차이 |
| 화면상 경계 | bilinear/decal 투영을 포함해 서버 격자 경계에서 한쪽당 최대 1 mask texel |
| ray 재구성 | 정적 평면에서 서버 방향 오차 `≤ 0.1°`, 월드/표면 로컬 접촉점 오차 `≤ 1cm` |
| 원격 접촉 복제 | 양자화 왕복 위치 오차 `≤ 0.2cm` |

### 7.3 재획득·Focus timeout 초기값

| 설정 | 초기값과 경계 검증 |
|---|---|
| `TransientMissGraceSeconds` | 0.10s; 0.09s는 재획득 후보, 0.11s는 새 sub-stroke. 플레이 조정 상한 0.15s |
| `ReconnectCandidateMaxCm` | 15cm; 14cm는 후보, 16cm는 새 sub-stroke. 플레이 조정 상한 25cm |
| `ReconnectBridgeMaxCm` | `min(실제 Evaluator SampleSpacingCm, 설정값)` 이하. 기본 자동 격자에서는 계산된 sample spacing을 기록 |
| `MaxMissingAimSequences` | 30Hz 전송 기준 2개; 초과하면 이전 점과 연결하지 않고 anchor reset |
| `MaxSampleAgeSeconds` | 0.15s; 오래된 샘플은 적용·lease 갱신 없이 폐기 |
| `PersistentInvalidExitSeconds` | `[초기값] 0.50s`; `T-0.01s`는 Focus 유지, `T+0.01s`는 종료 |
| `ViewOriginDriftToleranceCm` | 5cm; 4cm 보정은 유지, 6cm 보정은 다음 오염 적용 전 종료 |
| View/FOV 잠금 오차 | 마우스 입력 반복 중 회전 각 축 0.01° 이내, FOV 0.01° 이내 |
| Begin basis 허용 오차 | `[초기값]` 서버 권위 View 대비 회전 0.5°, FOV 0.1°; 각 경계의 ±0.01°로 승인/거부 검사 |
| aim view cone | 포인터 safe area를 deproject한 frustum에 0.25° 여유를 둔 범위; 바로 안/밖 ray로 승인/종료 검사 |
| 방향 재샘플·smoothing | `[초기값]` arc-length window 2.5cm, sample당 최대 축 변화 5°; raw FPS/RPC 분할과 무관해야 함 |
| 큰 코너 분리각 | `[초기값]` `acos(abs(dot)) ≥ 60°`; 59°는 연속, 61°는 공통 vertex에서 split하며 대각 연결 없음 |
| `BeginAckTimeout` | `[초기값] 1.0s`; timeout 뒤 늦은 Ack는 현재 상태를 바꾸지 않음 |
| `StartAimTimeout` | `[초기값] 0.50s`; 초과 시 Ability만 취소하고 Focus는 Ready 유지 |
| `AimSilenceTimeout` | `[초기값] 0.50s`; 초과 시 서버가 Ability와 Focus 전체 Cleanup |

grace 안이라도 miss 구간을 긴 선분으로 backfill하지 않는다. 짧은 bridge는 중간 query까지 서버 검증되고 위 `ReconnectBridgeMaxCm` 안일 때만 허용하며, 그 밖에는 첫 재획득점을 새 anchor로 사용한다.

### 7.4 프레임률·네트워크·성능 게이트

| 항목 | 완료 기준 |
|---|---|
| FPS 제거량 | 120fps 기준으로 30/60fps의 `Removed` 차이 `≤ max(1e-4, 2%)`, CleanRatio 절대 차이 `≤ 0.0025` |
| Stage3 결정성 | 같은 polyline의 stamp 위치·개수·총량이 FPS/batch 변화에도 셀별 `1e-4`, 총량 2% 이내 |
| 지연·유실 | 100~200ms RTT, 2~5% loss에서 소유자 예측점↔서버 확정점 p95 `≤ 2.5cm`, 최대 `≤ 5cm`; 초과 샘플은 이전 점과 연결하지 않음 |
| aim 송신률 | 긁기 중 플레이어당 최대 30 Unreliable RPC/s, Ready heartbeat 최대 5/s, batch 최대 3 samples |
| 4인 합산 | 동시 긁기 aim 입력 최대 120 RPC/s; 중복 sequence는 0회 재적용 |
| 업로드 대역폭 | aim payload 플레이어당 `≤ 5KiB/s` |
| 서버 성능 | 4인 동시, 128×64 양면 격자, 60초 측정에서 idle 대비 GT p95 `+1.0ms` 또는 10% 중 더 엄격한 값 이하 |
| 클라이언트 성능 | 같은 장면에서 GT p95 `+0.5ms` 이하, 긁기 유발 단일 p99 hitch `≤ 3ms` |

최소 사양이 확정되면 성능 수치는 해당 장비 기준으로 다시 승인한다. 수치를 변경할 때는 낮춘 기준만 남기지 말고 측정 장면·장비·Insights 캡처 위치를 함께 기록한다.

### 7.5 따개비 현행 회귀값

아래는 새 밸런스 제안이 아니라 네트워크 샘플링 변경으로 기존 타격감이 달라지는 것을 막는 현재 코드 계약이다.

| 입력/결과 | 회귀 기준 |
|---|---|
| 기본 헤라 59·60cm/s | 작업량 0 |
| 기본 헤라 260cm/s | 작업량 `0.3 ± 1e-4` |
| 기본 헤라 600cm/s 이상 | 상한 작업량 `0.81 ± 1e-4` |
| 기본 헤라 250cm/s | 타격당 0.285, `RemovalWorkRequired=1`이면 네 번째 유효 타격에 제거 |
| 열선 헤라 260cm/s | 현행 ×2.2 적용 시 `0.66 ± 1e-4` |
| 재타격 | 최소 간격 0.2s, 속도 30cm/s 이하에서 재무장 |
| 제거 보상 | 서버에서 산출물 1개와 ScrapeLoad +8을 정확히 한 번 적용 |
| FPS/batch | 같은 궤적의 유효 타격 횟수 동일, 누적 작업량 차이 `≤ max(0.001, 2%)` |
