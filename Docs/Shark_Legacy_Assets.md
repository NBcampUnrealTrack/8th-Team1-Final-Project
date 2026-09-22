# 구 Shark 에셋 현황

`/Game/Assets/Character/Shark` 는 Shark_MK4 이관 전의 구 에셋 폴더다.
2026-09-22 기준 66개 중 **49개를 삭제**하고 **17개를 남겼다**.

삭제 대상은 손으로 고르지 않고 참조 그래프로 계산했다. 폴더 밖에서 참조하는
에셋을 루트로 잡고 그 전이 의존물을 보존 집합으로 둔 뒤, 나머지를 지웠다.
삭제 후 `/Game` 전체를 훑어 끊어진 참조가 0건임을 확인했다.

> 계산할 때 접두사 비교를 `"/Game/Assets/Character/Shark"` 로 하면
> **`Shark_MK4` 도 같이 걸린다.** 실제로 이 실수 때문에 MK4 가 구 폴더를
> 참조하는 4건을 "폴더 내부 참조"로 잘못 분류했다. 반드시 끝에 `/` 를 붙일 것.

## 남긴 17개와 그 이유

### 1인칭 팔 (6개) — `BP_MDPlayerCharacter` 가 기본값으로 물고 있다

```
Arms/SKM_Shark_Arms_UE5              <- BP_MDPlayerCharacter
Arms/MI_SharkFPArms                  <- BP_MDPlayerCharacter
Arms/MI_SharkFPArms_Cap              <- BP_MDPlayerCharacter
Arms/M_SharkFPArms
Arms/SKM_Shark_Arms_UE5_Skeleton
Arms/SKM_Shark_Arms_UE5_PhysicsAsset
```

런타임에는 `AMD_Character::ApplyCharacterAppearance` 가 로스터의
`SKM_SharkMK4_Arms` 로 교체하므로 게임 동작에는 영향이 없다. 다만 BP 기본값이
구버전이라 이 에셋들이 계속 살아 있다.

### 벽 절단 몽타주 (2개) — `GA_WallCut` 이 참조한다

```
Anim/AnimSequence/Work/Cutter/AS_Cutter_Montage   <- /Game/GAS/Ability/GA_WallCut
Anim/AnimSequence/Work/Cutter/AS_Cutter
```

### MK4 가 아직 빌려 쓰는 것 (4개)

```
SKM_Shark                                    <- Shark_MK4/Normalized/PA_Shark_CM
T_shark_normal                               <- Shark_MK4/Arms/MI_SharkMK4FPArms
T_shark_roughness                            <- Shark_MK4/Arms/MI_SharkMK4FPArms
Anim/.../Ladder/anim_Ladder_Down/Driving_anim_Ladder_Down
                                             <- Shark_MK4/Anim/Ladder/anim_Ladder_Down
```

### 위 항목들의 의존물 (5개)

```
SK_Shark  PA_Shark  MI_Shark  T_shark_basecolor
Anim/AnimSequence/Ladder/anim_Ladder_Down
```

## 확인이 필요한 사항

아래는 측정으로 확인했지만 남의 파일이라 손대지 않았다.

**`GA_WallCut` 이 구 스켈레톤 몽타주를 쓴다.**
`AS_Cutter_Montage` 는 `SK_Shark` 에 바인딩돼 있는데, 플레이어 캐릭터는
`SKM_Shark_CM_Skeleton` 을 쓴다. 스켈레톤이 다른 몽타주는 재생되지 않는다.
MK4 대체본이 이미 있다: `Shark_MK4/Anim/Work/Cutter/A_Cutter_Montage`.

**`BP_MDPlayerCharacter` 의 `FirstPersonMesh` 기본값이 구 팔이다.**
로스터가 런타임에 덮어쓰므로 증상은 없지만, 기본값과 로스터가 어긋나 있다.

이 두 가지를 MK4 로 갱신하면 남은 17개 중 8개를 추가로 정리할 수 있다.
나머지는 MK4 에셋이 구 폴더의 텍스처·메시를 참조하고 있어, 그 참조를 MK4
자산으로 옮기기 전에는 지울 수 없다.
