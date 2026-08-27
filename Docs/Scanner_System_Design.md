# 스캐너 시스템 개편 기획서 (Scanner System Redesign)

## 1. 개요 (Overview)
기존 스캐너 시스템은 플레이어 시선(Line Trace)을 기준으로 좁은 범위(2미터) 내의 단일 격벽(`AMD_CuttableWall`)만을 식별하여 하이라이트했습니다. 
본 개편안은 스캐너의 범위를 **전방 원뿔(Cone) 형태, 10미터(1000 UU)** 로 확장하고, 해당 범위 내에 있는 **모든 스캔 가능한 오브젝트**를 동시에 다중 스캔할 수 있도록 개선합니다.

## 2. 주요 변경 사항 (Key Changes)
* **스캔 범위 확장**: 단일 Line Trace 탐지 방식에서 반경 10m 구역 내 전방 각도 판정(Cone Shape)으로 변경.
* **스캔 대상 확대**: 단일 `CuttableWall` 타겟팅에서 범위 내 다중 액터 처리로 변경. 격벽 외의 상호작용 가능한 아이템 등(Scannable Objects)도 스캔 태그를 가지면 탐지 대상에 포함.
* **VFX 변경 (MPC)**: 단일 오브젝트 기준 좌표계를 사용하던 `MPC_Scan`의 `ScanBoxCenter`를 **플레이어 스캐너 발동 위치**로 변경하고 범위를 10m에 맞게 확장.

## 3. 세부 구현 로직 (Implementation Details)

### 3.1. 전방 원뿔 형태 다중 탐지 (Cone Overlap)
`MD_GA_WallScan` 내부의 `ActivateAbility` 로직을 다음과 같이 수정합니다.

1. **Sphere Overlap**: 플레이어(카메라) 위치를 기준으로 반경 1000 UU (10미터)의 `SphereOverlapActors`를 실행하여 1차적으로 거리 내 대상들을 모두 수집.
2. **원뿔 각도 필터링(Dot Product)**: 
   - 수집된 각 액터의 위치 벡터와 플레이어 카메라의 전방 방향 벡터(Forward Vector) 간의 내적(Dot Product)을 계산.
   - 내적 값이 설정한 임계값(예: 0.707 ~ 45도 반각) 이상인 오브젝트만 최종 스캔 대상으로 확정.
3. **태그 기반 판정**: `ActorHasAnyTargetTag`를 활용하여 필터링된 타겟들이 `Interaction.Scannable` (또는 `Interaction.Cuttable`) 태그를 보유하고 있는지 확인.

### 3.2. 스캔 대상 인터페이스 및 하이라이트 적용
- 다수의 액터가 탐지되므로 반복문을 통해 각 액터에 하이라이트를 요청.
- 기존엔 `AMD_CuttableWall`에 명시된 `ScanHighlight`를 직접 호출했으나, 다른 오브젝트 호환성을 위해 **공통 인터페이스(예: `IMD_ScannableInterface`)** 를 도입하거나 다이나믹 델리게이트 이벤트 호출로 구조 개선 고려.
- 벽 뿐만 아니라 수집된 모든 유효 액터의 하이라이트 시각 효과 구동.

### 3.3. 머티리얼/VFX (MPC_Scan) 대응
기존 스캔 연출은 타겟 객체의 경계(Bounds)를 기준으로 사각형 범위를 설정했습니다 (`MPCInst->SetVectorParameterValue(TEXT("ScanBoxCenter"), FLinearColor(Origin));`).
- **변경안**: 스캐너 파동이 플레이어 위치에서부터 10미터 밖으로 퍼져나가는 방사형 연출로 머티리얼(PP_ScanXray 등) 수정.
- `ScanCenter` 파라미터를 플레이어 위치로 설정하고, 반경 변수를 1000으로 할당해 스캔 효과가 반경 10m 밖으로 벗어나면 페이드아웃 되도록 처리.

## 4. 변경 대상 파일 (Affected Files)
1. `Source/MarineDemolition/Private/Ability/Wall/MD_GA_WallScan.cpp`
   - 탐지 로직 (LineTrace -> SphereOverlap + DotProduct 필터링) 교체.
   - 다중 타겟 대상 `ScanHighlight` 루프 처리.
2. `Source/MarineDemolition/Public/Ability/Wall/MD_GA_WallScan.h`
   - 기존 반경 및 원뿔 각도(ConeAngle) 프로퍼티 추가 (`EditDefaultsOnly`).
3. 머티리얼 관련: `Content/VFX/03_Scanner/MPC_Scan` 파라미터 구조 변경 반영.

## 5. 알려진 이슈 및 렌더링 개선 (Rendering Fixes)

### 5.1. 스캔 머티리얼 심도(Depth) 우선순위 문제
- **이슈 설명**: 스캔된 오브젝트가 캐릭터 뒤에 있더라도 스캔 머티리얼 효과(포스트 프로세스 엑스레이 등)가 캐릭터(또는 카메라 앞의 다른 오브젝트)를 뚫고 렌더링되어 공간감을 해치고 캐릭터보다 앞에 있는 것처럼 보이는 현상.
- **해결 방안 (PP_ScanXray 개선)**: 
  - 포스트 프로세스 머티리얼(`PP_ScanXray`)에서 **Scene Depth**와 **Custom Depth**를 비교하는 로직을 추가합니다.
  - 현재 픽셀의 `SceneDepth`(일반 불투명 오브젝트의 깊이)가 `CustomDepth`(스캔된 대상의 깊이)보다 카메라에 더 가까운 경우(즉, 캐릭터나 장애물이 스캔 대상 앞에 있는 경우), 스캔 효과가 그려지지 않도록 마스킹 처리하거나 알파 값을 조절합니다.
  - 이를 통해 스캔된 오브젝트가 실제 공간상에서 시야에 가려진 상태임을 정확히 표현할 수 있습니다.
  - (옵션) 가려진 부분에 대해서는 스캔 색상을 다르게 표현하거나 점선/약한 외곽선으로만 표현하도록 분기할 수도 있습니다.
