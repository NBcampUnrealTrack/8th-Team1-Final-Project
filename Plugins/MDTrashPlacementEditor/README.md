# MD Trash Placement Editor

디자이너가 레벨 뷰포트에서 게임플레이 쓰레기를 직접 클릭하거나 드래그해 배치하는 UE 5.8 Editor 전용 플러그인이다.

배치 결과는 PCG나 인스턴스가 아니라 일반 `AMD_TrashItem` 파생 Actor로 현재 레벨에 저장된다. 게임 실행과 패키징에는 이 Editor 모듈이 필요하지 않으며, 배치된 Actor는 기존 줍기·분류·복제 로직을 그대로 사용한다.

## 현재 상태

- 플러그인 버전: `0.1.0`
- 모듈 종류: `Editor`
- 기본 Trash Actor: `/Game/Blueprint/Resource/Trash/TrashItem/BP_TrashItemBase`
- 프로젝트 설정: `MarineDemolition.uproject`에서 Editor 타깃에 활성화됨
- 배치 방식: 단일 종류 또는 Category 태그 기반 결정적 랜덤
- Undo 단위: 한 번의 클릭 또는 한 번의 연속 드래그

## 빠른 시작

1. C++ 변경 뒤에는 Unreal Editor를 다시 시작한다.
2. 에디터의 모드 선택 메뉴에서 **MD Trash Placement**를 선택한다.
3. 모드 패널에서 **Trash Placement** 도구를 선택한다.
4. `Mode`를 `Place`로 설정한다.
5. 한 종류만 배치하려면 `Selection Mode`를 `Single Kind`로 두고 `Trash Kind Id`를 선택한다.
6. 뷰포트의 표면을 클릭하면 1개가 배치되고, 마우스 왼쪽 버튼을 누른 채 드래그하면 연속 배치된다.
7. 마우스 휠 위·아래로 연속 배치 간격을 조절한다.
8. 잠깐 삭제할 때는 `Place` 모드에서 `Shift`를 누른 채 쓰레기를 클릭하거나 드래그한다. 계속 삭제할 때는 `Mode`를 `Erase`로 바꾼다.
9. 결과를 확인한 뒤 레벨을 저장한다.

## 조작과 단축키

| 입력 | Place 모드 | Erase 모드 |
|---|---|---|
| 마우스 Hover | 배치 위치와 유효성 Ghost 표시 | 삭제 대상 위치 표시 |
| 왼쪽 클릭 | Trash 1개 배치 | 커서가 맞힌 Trash 1개 삭제 |
| 왼쪽 드래그 | 표면을 따라 연속 배치 | 지나간 Trash 연속 삭제 |
| 마우스 휠 위·아래 | `Placement Spacing` 증가·감소 | 동일 |
| `Shift` + 왼쪽 클릭·드래그 | 해당 스트로크만 임시 Erase | Erase 유지 |
| `Ctrl+Z` | 직전 클릭·드래그 전체 실행 취소 | 직전 삭제 클릭·드래그 전체 실행 취소 |
| `Ctrl+Y` 또는 `Ctrl+Shift+Z` | 다시 실행 | 다시 실행 |

임시 지우개는 클릭이나 드래그를 **시작하기 전에** `Shift`를 누른다. 시작한 스트로크의 모드는 마우스 버튼을 놓을 때까지 고정되므로 도중에 `Shift`를 놓아도 해당 스트로크는 Erase로 끝난다.

마우스 휠 한 칸은 `Wheel Spacing Step`만큼 간격을 변경하며 최소값은 `1cm`다. 클릭이나 드래그 중에는 휠 변경을 받지 않는다. 도구가 활성화된 동안 뷰포트 휠은 간격 조절에 사용되며, 휠 회전 조절은 아직 구현되지 않았다.

## 선택 방식

### Single Kind

`Trash Kind Id`에서 고른 한 종류만 배치한다.

### Random By Category

`Random Category Tags`에 포함된 태그와 같거나 그 하위 Category를 가진 Trash Kind 중 하나를 고른다.

- `Score.Sorting`: 모든 분류 종류 허용
- `Score.Sorting.Metal`: 금속 종류만 허용
- 여러 태그 입력: 각 태그 결과의 합집합
- 빈 배열 또는 일치하는 종류 없음: 배치 차단과 경고 표시

선택은 `Random Seed + 배치 위치`로 결정된다. 같은 설정과 같은 위치에서는 같은 종류가 선택되므로 레벨을 다시 열어도 작업 결과를 재현하기 쉽다.

## 속성 설명

### Tool

| 속성 | 설명 |
|---|---|
| `Mode` | `Place` 또는 `Erase` 선택 |

### Placement / Selection

| 속성 | 설명 |
|---|---|
| `Trash Item Class` | 실제 생성할 Actor 클래스. 현재 기본값은 `BP_TrashItemBase` |
| `Selection Mode` | 단일 종류 또는 Category 랜덤 선택 |
| `Trash Kind Id` | `Single Kind`에서 사용할 종류 |
| `Random Category Tags` | `Random By Category`에서 허용할 Category 태그 합집합 |

### Placement

| 속성 | 설명 |
|---|---|
| `Placement Spacing` | 연속된 두 메시의 보이는 Bounds 표면 사이에 확보할 최소 거리(cm) |
| `Wheel Spacing Step` | 마우스 휠 한 칸으로 증감할 간격(cm), 기본 `10cm` |

`Placement Spacing`은 Actor 중심 사이 거리가 아니다. 이전 메시와 다음 메시의 크기, 회전, 스케일을 진행 방향으로 계산한 뒤 두 표면 사이에 지정된 간격을 남긴다.

### Placement / Orientation

| 속성 | 설명 |
|---|---|
| `Align To Surface` | 표면 법선에 Trash의 위쪽 축을 맞춤 |
| `Rotation Offset Degrees` | 표면 법선 축을 기준으로 추가 회전 |
| `Randomize Rotation` | 위치별 결정적 랜덤 회전 사용 |
| `Random Rotation Range Degrees` | 기준 회전에 더할 `-범위 ~ +범위` |

### Placement / Scale

| 속성 | 설명 |
|---|---|
| `Uniform Scale` | 기본 균일 스케일 |
| `Randomize Scale` | 위치별 결정적 랜덤 스케일 사용 |
| `Minimum Scale Multiplier` | 기본 스케일에 곱할 최솟값 |
| `Maximum Scale Multiplier` | 기본 스케일에 곱할 최댓값 |

### Placement / Randomization

| 속성 | 설명 |
|---|---|
| `Random Seed` | 종류, 회전, 스케일의 결정적 랜덤 기준값 |

### Placement / Safety

| 속성 | 설명 |
|---|---|
| `Prevent Overlaps` | 후보 메시 Bounds가 다른 충돌 물체를 관통하면 배치 차단 |
| `Overlap Tolerance` | 맞닿는 배치를 허용하기 위해 검사 Bounds를 줄이는 값(cm) |

기본 `Overlap Tolerance`는 `0.1 cm`다. 값을 크게 올리면 접촉은 쉬워지지만 실제 관통까지 허용할 수 있으므로 작은 값부터 조절한다.

## Ghost 색상

- 초록색: 현재 위치에 배치 가능
- 빨간색: 메시 누락 또는 다른 충돌 물체와 Overlap되어 배치 불가
- Ghost 없음: 유효한 표면을 찾지 못했거나 Place 모드가 아님

Ghost 컴포넌트는 임시 객체이며 레벨에 저장되지 않는다. 도구가 종료되면 제거된다.

## Trash 위에 쌓기

이전 클릭이나 이전 드래그에서 배치된 Trash는 다음 작업의 표면으로 사용할 수 있다. 접촉만 하는 배치는 허용하지만 Bounds가 관통하면 빨간 Ghost가 표시되고 배치가 차단된다.

같은 연속 드래그에서 방금 생성한 Trash는 후속 표면 Trace에서 제외된다. 따라서 한 번의 드래그가 자기 위로 계속 올라가며 비정상적으로 쌓이지 않는다. 여러 층을 만들려면 첫 드래그를 놓은 뒤 새 클릭 또는 새 드래그로 다음 층을 배치한다.

## 설정 저장

도구를 정상적으로 종료하면 현재 속성이 저장되고, MD Trash Placement 모드에 다시 들어왔을 때 복원된다.

에디터 강제 종료나 크래시처럼 도구의 정상 종료 절차가 실행되지 않은 경우 마지막 변경값은 저장되지 않을 수 있다.

## 권장 작업 순서

1. 넓은 빈 공간에서 `Single Kind`와 기본 스케일로 간격을 먼저 조정한다.
2. 회전 랜덤화를 켜고 필요한 경우 스케일 범위를 작게 적용한다.
3. 여러 종류가 필요할 때 `Random By Category`로 전환한다.
4. 초록 Ghost가 보이는 위치만 배치한다.
5. 한 층을 만든 뒤 버튼을 놓고, 다음 스트로크로 적층한다.
6. 한 번의 스트로크가 끝날 때마다 `Ctrl+Z`가 원하는 단위로 동작하는지 확인한다.
7. 배치가 끝나면 Place 모드를 나와 일반 선택 도구로 개별 Actor를 미세 조정한다.
8. 레벨을 저장하고 PIE에서 기존 Trash 상호작용을 확인한다.

## 문제 해결

### MD Trash Placement 모드가 보이지 않음

- `MarineDemolition.uproject`에서 `MDTrashPlacementEditor`가 활성화됐는지 확인한다.
- Editor 타깃 빌드가 성공했는지 확인한다.
- C++ 빌드 뒤 에디터를 완전히 다시 시작한다.
- 이 플러그인은 Editor 전용이므로 게임 실행 화면이나 Shipping 빌드에는 나타나지 않는다.

### Trash Item Class가 비어 있음

기본 Blueprint가 다음 경로에 존재하는지 확인한다.

`/Game/Blueprint/Resource/Trash/TrashItem/BP_TrashItemBase`

에셋을 이동하거나 이름을 바꾸면 플러그인의 기본 클래스 경로도 함께 수정해야 한다.

### Random By Category에서 배치되지 않음

- `Random Category Tags`가 비어 있지 않은지 확인한다.
- 우선 `Score.Sorting` 루트 태그로 전체 종류가 검색되는지 확인한다.
- Trash Kind 데이터 행의 `Category`가 올바르게 지정됐는지 확인한다.

### 계속 빨간 Ghost만 표시됨

- 선택된 Trash Kind에 Static Mesh가 있는지 확인한다.
- 표면과 주변 Actor의 Collision Query가 활성화되어 있는지 확인한다.
- 후보 메시가 다른 물체를 관통하지 않는지 확인한다.
- `Overlap Tolerance`를 크게 올리기 전에 메시 Bounds와 Collision 형태를 먼저 확인한다.

### 간격이 예상보다 큼

`Placement Spacing`은 메시 중심 거리가 아니라 메시 표면 사이 거리다. 큰 메시 두 개는 각 Bounds 크기에 지정 간격까지 더해져 중심이 더 멀리 떨어지는 것이 정상이다.

## 현재 범위 밖

- 마우스 휠을 이용한 회전 변경
- 반경 기반 지우개
- 물리 낙하 및 Chaos 정착 결과 저장
- 여러 설정을 이름으로 저장하는 프리셋 팔레트
- Data Layer 자동 선택 또는 배치 대상 필터
- 런타임 중 플레이어가 사용하는 배치 기능

## 개발자 빌드 검증

DebugGame Editor 빌드:

```powershell
C:\UE_5.8\Engine\Build\BatchFiles\Build.bat MarineDemolitionEditor Win64 DebugGame -Project=C:\UE_Project\ProjectATM\MarineDemolition.uproject -WaitMutex -FromMsBuild
```
