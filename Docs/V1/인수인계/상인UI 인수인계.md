<!-- planning-v2-20261001 -->
> **2026-10-01 적용 범위 변경:** 이 문서는 회의 이전 기획·구현 기록이다. 메인·반복퀘스트, DAY, 벽 헤라질, 지지대, 지주격벽 규칙은 현재 요구사항에서 제외한다. 로어와 벽 파괴는 유지한다. 본문의 관련 수치·배치·대사·튜토리얼 단계를 새 필수 작업으로 적용하지 않는다. 삭제 대상으로 명시되지 않은 기능은 존폐를 검토한다. 문서 표시가 실제 코드/BP 제거 완료를 뜻하지 않는다. [현재 회의 기준](../v2/00%20시스템%20축소%20회의%20결정.md)과 [새 작업 목록](../프로젝트%20전체%20작업%20목록과%20우선순위.md)을 우선한다.

# 인수인계 문서

작성일: 2026-08-10

이 문서는 제가 맡았던 작업 중 **상인/상점/퀘스트 UI**를 중심으로 정리합니다. 코드를 직접 안 다뤄도 되는 분들도 전체 구조는 알아야 한다고 해서, 아키텍처 설명을 최대한 자세히 남겼습니다. 나머지 영역(기획서, 에셋 등)은 뒤에 목록만 간단히 붙입니다.

---

## 1. 담당 영역 요약

| 영역 | 상태 |
|---|---|
| 상인 루트 메뉴 (대화/상점/퀘스트/대출/나가기) | 완료 |
| 상점 UI (5탭 + 아이템 카드) | 완료 |
| 퀘스트 UI (메인/반복/로어 3탭) | **UI는 완료, 일부 백엔드 연결 대기** — 아래 4장 참고 |
| 대출 UI | **미착수** — 버튼은 있으나 화면 없음 |
| 상인 관련 게임플레이 연동 (Day 동기화, 계약 시작/종료, 크레딧) | 완료 (동연님 코드 기반, 저는 UI 쪽에서만 연동) |

---

## 2. 아키텍처

### 2.1 전체 흐름

```
AMD_Merchant (월드 액터, 상인 "노라 브릭스")
  └─ Interact 시 → UMD_MerchantMenuWidget 생성 (대화/상점/퀘스트/대출/나가기 버튼)
       ├─ '상점' → UMD_ShopWidget 생성 (메뉴 위에 겹쳐 뜸, 메뉴는 살아있음)
       │              └─ 카드: UMD_ShopItemCardWidget
       ├─ '퀘스트' → UMD_QuestWidget 생성 (3탭: 메인/반복/로어)
       │              ├─ 메인 탭 행: UMD_MainQuestRowWidget
       │              └─ 반복 탭 카드: UMD_QuestCardWidget
       └─ '대출' → 아직 화면 없음. HandleNotReadyRequested가 대사만 띄움
```

**공통 설계 원칙** (상점에서 만든 패턴을 퀘스트에도 그대로 적용):
- 화면 위젯은 **판정을 하지 않는다.** 버튼을 누르면 델리게이트로 "요청"만 올리고, 실제 서버 판정은 항상 `AMD_Merchant`(소유 액터)나 `AMD_PlayerController`가 처리한다.
- 위젯 프로퍼티는 전부 `BindWidgetOptional`이다. WBP에서 아직 안 배치한 요소가 있어도 컴파일이 깨지지 않는다 — **바꿔 말하면, C++ 프로퍼티 이름과 WBP의 위젯 이름이 정확히 같아야 자동 바인딩된다.** 이름이 다르면 조용히 `nullptr`로 남고 아무 에러도 안 뜬다. 새로 만질 때 제일 자주 걸리는 함정.

### 2.2 데이터가 어디서 오는가 (읽기 경로)

퀘스트 데이터/로직은 **동연님** 담당입니다. 제가 만든 건 그 데이터를 화면에 그리는 쪽뿐입니다.

```
UMD_MerchantSubsystem (GameInstanceSubsystem, 서버 권한)
  - MainQuests / RepeatQuests 상태 보관, 수락·거절·완료·실패 판정
  - BeginContract / BeginDay / NotifySampleDelivered / NotifySampleDestroyed /
    ResolveActiveQuestsAtDayEnd / FinalizeContract 등
       ↓ (변경 시 OnMerchantStateChanged 브로드캐스트)
UMD_MerchantComponent (AMD_GameStateBase에 붙음, 클라이언트로 복제)
  - GetMainQuests() / GetRepeatQuests() / GetCredits() 등 BlueprintPure 게터
       ↓
AMD_Merchant::GetMerchantComponent()
  (GetWorld()->GetGameState<AMD_GameStateBase>()->GetMerchantComponent())
       ↓
UMD_QuestWidget::SetMainQuests() / SetRepeatQuests()
```

퀘스트 화면이 열려 있는 동안은 `MerchantComponent->OnMerchantStateChanged`를 구독해서, 상태가 바뀔 때마다(수락 성공, 완료, Day 전환 등) 자동으로 다시 그립니다. 리슨 서버 호스트든 원격 클라이언트든 이 델리게이트 하나로 갱신되므로, **직접 폴링하거나 수동으로 새로고침을 호출할 필요가 없습니다.**

### 2.3 액션이 어디로 가는가 (쓰기 경로)

```
UMD_QuestCardWidget (버튼 클릭)
  → OnAcceptRequested / OnDeclineRequested / OnAbandonRequested 브로드캐스트 (인덱스만 들고)
       ↓
UMD_QuestWidget이 그대로 상위로 전달
       ↓
AMD_Merchant::HandleRepeatAcceptRequested(int32 QuestIndex)
  → Cast<AMD_PlayerController>(...)->ServerAcceptRepeat(QuestIndex)   // 이미 있던 Server RPC
AMD_Merchant::HandleRepeatDeclineRequested(int32 QuestIndex)
  → ...->ServerDeclineRepeat(QuestIndex)
AMD_Merchant::HandleRepeatAbandonRequested(int32 QuestIndex)
  → 백엔드에 대응 함수가 없어서 지금은 대사 안내만 함 (4.2 참고)
```

`ServerAcceptRepeat`/`ServerDeclineRepeat`는 동연님이 이미 만들어 둔 BlueprintCallable Server RPC(`MD_PlayerController.h`)라서, 저는 **PlayerController나 Subsystem 파일을 한 줄도 건드리지 않고** 기존 공개 API만 호출해서 연결했습니다.

### 2.4 파일 목록 (제가 만든 것 / 건드린 것)

| 파일 | 역할 | 비고 |
|---|---|---|
| `UI/Merchant/MD_MerchantMenuWidget.h/.cpp` | 상인 루트 메뉴 | 신규 작성 |
| `UI/Merchant/MD_MerchantUITypes.h` | `FMD_MerchantMenuInfo`, `EMD_QuestTab` 등 UI 공용 타입 | 신규 작성 |
| `UI/Merchant/MD_ShopWidget.h/.cpp` | 상점 5탭 + 목록 | 신규 작성 |
| `UI/Merchant/MD_ShopItemCardWidget.h/.cpp` | 상점 아이템 카드 | 신규 작성 |
| `UI/Merchant/MD_QuestWidget.h/.cpp` | 퀘스트 3탭(메인/반복/로어) 컨테이너 | 신규 작성 |
| `UI/Merchant/MD_QuestCardWidget.h/.cpp` | 반복퀘 카드 1장 | 신규 작성 |
| `UI/Merchant/MD_MainQuestRowWidget.h/.cpp` | 메인퀘 Day 1행 | 신규 작성 |
| `World/Merchant/MD_Merchant.h/.cpp` | 상인 액터. 위젯 생성/연결, GameState·PlayerController 호출 | 신규 작성 후 계속 확장 |
| `Content/UI/Merchant/WBP_MD_*` | 위 C++ 클래스에 대응하는 WBP | UmgMcp로 제작 |
| `Content/Blueprint/Merchant/BP_MD_Merchant` | 상인 블루프린트(각 WidgetClass 지정) | **QuestWidgetClass가 실제로 지정돼 있는지 인게임에서 확인 필요** — 바이너리라 git diff로는 확인 불가 |

**건드리지 않은 것** (동연님 소유, 데이터·로직 전담):
`MD_QuestDefinition.h`, `MD_SiteDefinition.h`, `MD_MerchantTypes.h`, `MD_MerchantSubsystem.h/.cpp`, `MD_MerchantComponent.h/.cpp`, `MD_PlayerController.h/.cpp`(퀘스트 RPC 부분), `MD_GameModeBase.h/.cpp`(Day 동기화 부분). 이 파일들은 **읽고 호출만** 했습니다.

---

## 3. 완료된 것

- 상인 인터랙트 → 루트 메뉴 → 상점/퀘스트 화면 전환 전체 흐름
- 상점 5탭(도구/안전/유틸수리/생존/회수) + 카드(이름·가격·해금 상태·잠김 조건)
- 퀘스트 3탭:
  - **메인탭**: Day1/2/3 세로 나열, 현재/완료 Day만 펼쳐서 설명·진행도 표시, 잠긴 Day는 접힘
  - **반복탭**: 카드별 수락/거절, 수락 후 진행도 N/M, 서버 상태 변경 시 자동 새로고침
  - **로어탭**: 자리만 확보 (4.1 참고)
- 크레딧/호감도/착수금/계약일차 등 헤더 정보 표시 (`MD_MerchantMenuInfo`)
- 반복퀘 수락/거절이 실제 서버 RPC로 연결되어 인게임에서 동작함

---

## 4. 인수인계 — 남은 일 / 확인 필요

### 4.1 로어퀘(코덱스) 백엔드가 없음
기획서(`09` §5, `04` §5.5 탭3)에는 기록물 수집·완성률 시스템이 정의돼 있지만, **이걸 실제로 저장·판정하는 코드가 아직 어디에도 없습니다.** `UMD_QuestWidget`의 로어 탭은 "코덱스 데이터가 아직 연결되지 않았다" 안내문만 뜨는 빈 화면입니다. 백엔드(수집 상태 저장, 완성률 계산, 해독 조건 등)가 생기면 `MD_QuestWidget::ApplyTabVisibility()`의 `PNL_LoreTab`/`TXT_LoreEmptyNotice` 부분을 실제 목록으로 바꿔야 합니다.

### 4.2 반복퀘 "포기(파기)" 처리가 없음
UI에는 수락한 카드에서 포기(`BTN_Abandon`) 버튼이 있고 `OnAbandonRequested` 델리게이트까지 올라오지만, `UMD_MerchantSubsystem`에는 `TryAcceptRepeatQuest`/`TryDeclineRepeatQuest`만 있고 **수락 이후 상태를 되돌리는 함수가 없습니다.** 지금은 `AMD_Merchant::HandleRepeatAbandonRequested`가 "그건 아직 처리 못 해" 대사만 띄우는 걸로 막아뒀습니다. 백엔드에 `TryAbandonRepeatQuest(int32)` 같은 함수가 추가되면 그 핸들러를 실제 Server RPC 호출로 바꾸면 됩니다.

### 4.3 퀘스트 카드에 점수·호감도 트랙이 없음
기획서(`04` §5)는 반복퀘 카드에 **평가 점수 / 크레딧 / 호감도 3트랙**을 나란히 보여주라고 하지만, `FMD_QuestDefinitionData`에는 `RewardCredits`만 있고 점수·호감도 필드가 없습니다. 그래서 지금 카드에는 크레딧 보수만 표시됩니다. 데이터 필드가 추가되면 `MD_QuestCardWidget::RefreshDisplay()`에 점수/호감도 텍스트를 추가해야 합니다.

### 4.4 대출 UI 자체가 없음
메뉴에 '대출' 버튼은 있고 `OnLoanRequested` 델리게이트도 있지만, 상점/퀘스트처럼 실제 화면(`MD_LoanWidget` 같은 것)이 없습니다. 기획서 `04` §4에 요구사항은 정리돼 있습니다(한도/이자율/예상 이자/빌리기/상환). 상점·퀘스트와 동일한 패턴(화면은 요청만 올리고 판정은 액터/컨트롤러가)으로 만들면 됩니다.

### 4.5 BP_MD_Merchant 실제 설정 확인
`QuestWidgetClass`(및 `MainQuestRowClass`/`RepeatQuestCardClass`)가 블루프린트 디폴트에 실제로 지정돼 있는지는 `.uasset`이 바이너리라 git 기록으로 확인이 안 됩니다. **에디터에서 `BP_MD_Merchant`를 열어 세 클래스가 다 지정돼 있는지 확인해주세요.** 하나라도 비어 있으면 해당 화면이 조용히 안 뜹니다(에러 없이).

---

## 5. 알아두면 좋은 함정 / 컨벤션

- **위젯 이름 매칭**: C++의 `TObjectPtr<UButton> BTN_Accept` 같은 프로퍼티는 WBP 안의 위젯 이름이 정확히 `BTN_Accept`일 때만 자동 바인딩됩니다. 오타·다른 이름이면 에러 없이 그냥 안 붙습니다.
- **소스 코드 소유권**: 이 프로젝트는 "남의 파일은 컨벤션 위반이 있어도 고치지 않는다"는 원칙으로 작업했습니다. 퀘스트 관련 데이터/로직 파일은 전부 동연님 소유이고, 저는 그 공개 함수만 호출했습니다. 새로 작업할 때도 이 경계를 유지하는 걸 추천합니다.
- **화폐 표기는 `₡`**: `G`나 `원` 쓰면 기획서 표기와 안 맞습니다.

---

## 6. 그 외에 제가 손댄 영역 (간단 목록)

코드를 직접 안 다루는 분들 기준으로, "어디에 제 흔적이 있는지"만 간단히:

- **퀘스트/상점/현장 데이터에셋** (`Content/Data/Merchant/`, `Content/Data/Shop/`, `Content/Data/Site/`): 데이터에셋 자체는 동연님 구조를 따라 만듦
