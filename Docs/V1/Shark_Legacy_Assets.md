# 구 Shark 에셋 현황

`/Game/Assets/Character/Shark` 는 Shark_MK4 이관 전의 구 에셋 폴더다.
2026-09-22 정리로 **66개 중 55개를 삭제**하고 **11개를 남겼다**.

삭제 대상은 손으로 고르지 않고 참조 그래프로 계산했다. 폴더 밖에서 참조하는
에셋을 루트로 잡고 그 전이 의존물을 보존 집합으로 둔 뒤 나머지를 지웠으며,
매 단계마다 `/Game` 전체를 훑어 끊어진 참조가 0건임을 확인했다.

> 계산할 때 접두사 비교를 `"/Game/Assets/Character/Shark"` 로 하면
> **`Shark_MK4` 도 같이 걸린다.** 실제로 이 실수로 MK4 가 구 폴더를 참조하는
> 4건을 "폴더 내부 참조"로 잘못 분류했다. 반드시 끝에 `/` 를 붙일 것.

## 같이 처리한 참조 갱신

구 에셋을 붙잡고 있던 참조 중 명백히 이관 누락인 것들을 MK4 로 옮겼다.

**`GA_WallCut.CutMontage` — 손대지 않았다. 미해결 상태다.**

`AS_Cutter_Montage` 는 구 스켈레톤 `SK_Shark` 에 바인딩돼 있는데 플레이어는
`SKM_Shark_CM_Skeleton` 을 쓴다. 스켈레톤이 다른 몽타주는 재생되지 않으므로
벽 절단 연출은 나오지 않는다. MK4 대체본
`Shark_MK4/Anim/Work/Cutter/A_Cutter_Montage` 가 이미 있고 길이도 3.5667초로
동일하다.

교체를 시도했다가 도구가 `GA_WallCut` 에 EventGraph 를 중복 생성해 컴파일을
깨뜨렸고, 되돌렸다(`e9dff2b0`). **에디터에서 변수 기본값을 직접 바꾸는 것이
안전하다** — 에셋 열기 → My Blueprint 의 `CutMontage` 선택 → Details 의
Default Value 를 `A_Cutter_Montage` 로 → Compile → Save.

바꾼 뒤에는 `AS_Cutter_Montage` / `AS_Cutter` 두 개도 지울 수 있다.
교체 후 벽을 잘라 몽타주가 실제로 재생되는지 확인할 것. 그래프가
`MontageSetNextSectionName(From="Default", To="Default")` 를 호출하므로
MK4 몽타주에 `Default` 섹션이 있어야 한다.

**`BP_MDPlayerCharacter.FirstPersonMesh`** — 메시와 오버라이드 머티리얼 2종을
MK4 로 교체했다.

| | 이전 | 이후 |
|---|---|---|
| 메시 | `SKM_Shark_Arms_UE5` | `SKM_SharkMK4_Arms` |
| 머티리얼 | `MI_SharkFPArms`, `MI_SharkFPArms_Cap` | `MI_SharkMK4FPArms`, `MI_SharkMK4FPArms_Cap` |

런타임에는 `AMD_Character::ApplyCharacterAppearance` 가 로스터 값으로 덮어쓰므로
게임 동작은 바뀌지 않는다. 기본값과 로스터가 어긋나 있던 것을 맞춘 것이다.

**`SK_Shark` 의 호환 스켈레톤 등록 해제** — `SKM_Shark_Arms_UE5_Skeleton` 이
호환 스켈레톤으로 등록돼 있어서 아무도 쓰지 않는 1인칭 팔 6개가 참조 그래프상
살아 있었다. 실사용이 아니라 애니메이션 공유용 등록이라 해제했다.

## 남긴 11개와 그 이유

```
SKM_Shark            <- Shark_MK4/Normalized/PA_Shark_CM
T_shark_normal       <- Shark_MK4/Arms/MI_SharkMK4FPArms
T_shark_roughness    <- Shark_MK4/Arms/MI_SharkMK4FPArms
Anim/AnimSequence/Ladder/anim_Ladder_Down/Driving_anim_Ladder_Down
                     <- Shark_MK4/Anim/Ladder/anim_Ladder_Down

SK_Shark  MI_Shark  PA_Shark  T_shark_basecolor
Anim/AnimSequence/Ladder/anim_Ladder_Down          (위 4개의 의존물)

Anim/AnimSequence/Work/Cutter/AS_Cutter_Montage    <- GAS/Ability/GA_WallCut
Anim/AnimSequence/Work/Cutter/AS_Cutter            (위의 의존물)
```

앞의 9개는 Shark_MK4 에셋이 실제로 의존한다. 뒤의 2개는 위에 적은 대로
`GA_WallCut` 교체가 미해결이라 남아 있는 것이다.

`Driving_anim_Ladder_Down` 은 **LevelSequence** 다. MK4 의 `anim_Ladder_Down` 이
이걸 저작 원본으로 참조한다. 끊으면 시퀀서에서 그 애니메이션을 다시 편집할 수
없게 되므로 남겼다.

## 더 정리하려면

남은 9개를 없애려면 MK4 쪽 참조를 옮겨야 한다. 전부 남의 자산이라 손대지 않았다.

- `MI_SharkMK4FPArms` 가 구 폴더의 `T_shark_normal` / `T_shark_roughness` 를 쓴다.
  MK4 전용 텍스처로 교체하거나 텍스처를 MK4 폴더로 옮기면 된다.
- `PA_Shark_CM`(MK4 피직스 에셋)이 구 `SKM_Shark` 를 프리뷰 메시로 물고 있다.
- `Shark_MK4/Anim/Ladder/anim_Ladder_Down` 의 시퀀서 원본을 MK4 폴더로 옮기면
  사다리 체인 전체를 정리할 수 있다.
