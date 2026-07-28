# 협업 및 코딩 컨벤션 가이드 (Unreal Engine)


## 1. 브랜치 전략

| 브랜치 | 용도 | 비고 |
|---|---|---|
| `main` | 배포 및 안정 빌드 | 직접 푸시 금지, PR을 통해서만 반영 |
| `dev` | 통합 개발 | 기능 병합 대상 |
| `feat/<이름>` | 기능 단위 작업 | `dev`에서 분기 |

- 브랜치명은 소문자와 케밥케이스로 작성합니다. (예: `feat/timer`, `feat/ui`)
- `main`과 `dev`에는 보호 규칙을 설정하여 강제 푸시를 차단하고 PR 리뷰를 필수화합니다.

---

## 2. 커밋 컨벤션

형식은 `<type> <요약>`을 따르며, 한글과 영문 모두 허용하되 명령형 현재 시제로 작성합니다.

| type | 의미 |
|---|---|
|add| 신규 에셋/파일 추가 (최초 1회)|
|feat| 새로운 기능 완성 및 로직 구현|
|fix| 버그 수정 및 에러 해결|
|upd| 기존 기능 강화, 수치 조정, 에셋 교체|
|doc| 코드 주석 및 문서 작업|

```
feat: Event 기반 히트 판정 어빌리티 추가
fix: 서버 RPC WithValidation 누락으로 인한 연결 해제 문제 수정
```

**규칙**

- 하나의 커밋은 하나의 논리 단위로 구성합니다. 코드 변경과 대량의 에셋 변경은 분리합니다.
- 제목은 50자 이내로 작성하며, 본문에는 변경의 이유를 기술합니다. 변경 내용 자체는 diff가 설명합니다.

---

## 3. Pull Request 및 코드 리뷰

- PR은 작은 단위로 자주 생성합니다. 변경 파일이 많을수록 리뷰 품질이 저하됩니다.
- PR 본문에는 변경 요약, 테스트 방법, 관련 이슈 링크를 기재합니다.
- 병합 조건은 리뷰어 1인 이상의 승인과 로컬 빌드 성공입니다.
- 병합 방식은 히스토리 정리를 위해 `Squash and merge`를 기본으로 합니다.
- 리뷰는 코드를 대상으로 하며 작성자를 대상으로 하지 않습니다. 명확한 근거를 들어 제안합니다.

---

## 4. 바이너리 에셋 협업 (핵심 사항)

`.uasset` 및 `.umap` 파일은 텍스트 병합이 불가능합니다. 두 명이 동일한 에셋을 수정할 경우 한쪽의 작업이 반드시 유실됩니다. 따라서 편집 전 잠금(Lock)을 통해 이를 예방합니다.

```bash
git lfs lock   Content/Maps/MainLevel.umap      # 편집 시작 전 잠금
# ... 에디터에서 작업 후 커밋 및 푸시 ...
git lfs unlock Content/Maps/MainLevel.umap      # 작업 완료 후 해제

git lfs locks                                    # 현재 잠긴 파일 및 소유자 확인
```

**규칙**

- 레벨(`.umap`), 공용 블루프린트, 머티리얼 그래프는 반드시 잠금 후 편집합니다.
- 푸시 직후 즉시 잠금을 해제하며, 점유 상태로 방치하지 않습니다.
- 잠금이 해제되지 않은 경우, 소유자와 합의한 후에만 `--force`로 해제합니다.
- 작업에 앞서 항상 최신 상태로 동기화합니다. (Pull 우선, 작업 이후)

---

## 5. C++ 코딩 컨벤션

Epic의 [Coding Standard](https://docs.unrealengine.com/coding-standard/)를 따릅니다.

### 5.1 네이밍 접두사

| 접두사 | 대상 |
|---|---|
| `U` | `UObject` 파생 (`UComponent`) |
| `A` | `AActor` 파생 (`ACharacter`) |
| `F` | 일반 구조체 및 클래스, non-UObject (`FGameplayTag`) |
| `E` | 열거형 (`EWeaponType`) |
| `I` | 인터페이스 (`IInteractable`) |
| `T` | 템플릿 (`TArray`, `TSubclassOf`) |
| `S` | Slate 위젯 (`SButton`) |
| `b` | bool 변수 (`bIsDead`, `bHasAuthority`) |

- 모든 식별자는 PascalCase(UpperCamelCase)로 작성하며, `snake_case`는 사용하지 않습니다.
- bool을 제외한 헝가리안 표기법은 사용하지 않습니다.
- 약어 또한 PascalCase를 따릅니다. (`GetHttpResponse`는 허용, `GetHTTPResponse`는 지양)

### 5.2 변수 및 함수

```cpp
// 함수: 동사로 시작하며, bool 반환 함수는 Is/Has/Can을 사용
bool IsAlive() const;
void ApplyDamage(float Amount);

// 멤버 변수: 의미를 명확히 하며 약어를 지양
float MaxHealth;
TObjectPtr<UStaticMeshComponent> MeshComponent;

// 상수
static constexpr int32 MaxPlayers = 4;
```

### 5.3 UE5 유의 사항

- UObject를 참조하는 `UPROPERTY` 포인터는 `TObjectPtr<T>`를 사용합니다. (GC 추적 및 에디터 안정성)
- `#include "X.generated.h"`는 항상 헤더 include 블록의 마지막 줄에 위치합니다.
- nullable 객체는 사용 전 항상 `nullptr` 여부를 확인하며, GC 대상 객체는 `IsValid()`로 확인합니다.

```cpp
UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat")
TObjectPtr<UAnimMontage> AttackMontage;
```

### 5.4 UPROPERTY 및 UFUNCTION

- 에디터 또는 블루프린트에 노출하는 멤버는 반드시 `Category`를 지정합니다.
- 접근 제어를 명시합니다. (`EditAnywhere`/`VisibleAnywhere`, `BlueprintReadWrite`/`BlueprintReadOnly`)
- 리플리케이트 변수는 `Replicated` 지정과 `GetLifetimeReplicatedProps` 등록을 수행하며, 필요 시 `ReplicatedUsing`으로 OnRep 함수를 연결합니다.

### 5.5 헤더 및 include

- 전방 선언을 우선합니다. 헤더에서 전체 정의가 필요하지 않은 경우 `class UFoo;`로 선언하고 `.cpp`에서 include합니다.
- include 순서는 ① 대응 헤더, ② 엔진 헤더, ③ 프로젝트 헤더, ④ `*.generated.h` 순으로 합니다.
- 컴파일 시간에 직접적인 영향을 미치므로 헤더에 무거운 include를 포함하지 않습니다.

### 5.6 기타 규칙

- 첫 소스 파일 생성 시 네이밍 접두사는 'MD_'가 앞에 있어야 합니다. (예: MD_SurviorCharacter)
- const 정확성을 준수합니다. 멤버를 변경하지 않는 함수는 `const`로 선언하고, 변경하지 않는 참조 인자는 `const&`를 사용합니다.
- 가상 함수의 재정의에는 반드시 `override`를 명시합니다. (`virtual` 단독 사용 금지)
- 다형성의 기반이 되는 클래스에는 가상 소멸자를 보장합니다.
- 매직 넘버는 사용하지 않으며, 명명된 상수 또는 `UPROPERTY`로 대체합니다.
- TODO 주석은 `// TODO(이름): 내용` 형식으로 작성합니다.

---

## 6. 에셋 및 블루프린트 네이밍

`[접두사]_[이름]_[변형]` 형식을 따릅니다.

| 타입 | 접두사 | 예시 |
|---|---|---|
| 블루프린트 | `BP_MD_` | `BP_MD_PlayerCharacter` |
| 위젯 블루프린트 | `WBP_MD_` | `WBP_MainMenu` |
| 스태틱 메시 | `SM_` | `SM_Wall_01` |
| 스켈레탈 메시 | `SKM_` | `SK_Mannequin` |
| 머티리얼 | `M_` | `M_Floor` |
| 머티리얼 인스턴스 | `MI_` | `MI_Floor_Wet` |
| 텍스처 | `T_` | `T_Floor_BaseColor` |
| 애니메이션 몽타주 | `AM_` | `AM_Attack` |
| 애님 블루프린트 | `ABP_MD_` | `ABP_MD_Character` |
| 데이터 애셋 | `DA_MD_` | `DA_MD_WeaponStats` |
| 사운드 큐 | `SC_` | `SC_Footstep` |

- 폴더는 기능 또는 모듈 단위로 구성합니다. (예: `Content/Characters/Player/...`)
- 한글, 공백, 특수문자는 사용하지 않으며, 영문과 언더스코어만 사용합니다.

---
## 7. 개인 작업 공간 (Developers 폴더)

`Content/Developers/<사용자명>/`은 각 구성원에게 할당되는 개인 실험용 공간입니다. 정식 폴더 구조를 어지럽히지 않고 임시 및 시험용 에셋을 다루기 위한 용도이며, 협업 시 다음을 따릅니다.

**활성화**

콘텐츠 브라우저의 [Settings] → [Show Developers Content]를 체크하면 표시됩니다.

**용도**

- 프로토타입, 테스트 레벨, 일회성 디버그 에셋 등 정식 편입 이전의 작업물 보관
- 다른 구성원의 작업에 영향을 주지 않는 격리된 실험 공간 확보

**규칙**

- 배포(production) 에셋은 Developers 폴더의 에셋을 **참조하지 않습니다.** 참조 시 쿡 및 패키징 단계에서 누락되어 문제가 발생하므로, 레퍼런스 뷰어로 의존성을 점검합니다.
- 작업물이 정식화되면 적절한 공유 폴더로 이동한 후 리다이렉터를 정리합니다. (폴더 우클릭 → Fix Up Redirectors in Folder)
- Developers 폴더의 커밋 여부는 팀 정책으로 결정합니다. 백업 및 공유 목적이면 커밋하고, 저장소를 가볍게 유지하려면 `Content/Developers/`를 `.gitignore`에 추가합니다.
---

## 8. 기타

-- 외부 에셋 & 리소스 받기 전에 사전 공유가 필요합니다. (사진 + 이름)

---
