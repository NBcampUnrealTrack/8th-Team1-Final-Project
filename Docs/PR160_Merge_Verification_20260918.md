# PR160 충돌 해결 검증

대상: PR160, 기존 head b6caf26d, dev 6f1b854e.

1. 시작 작업 트리는 dev와 일치하며 Git/에디터 모두 미저장 변경 없음 확인.
2. 에디터 파일 잠금으로 첫 PR 브랜치 전환이 부분 적용됨. 22개 잔존 파일의 Git/LFS hash가 원래 dev와 같음을 검증하고 인덱스를 맞춘 뒤 dev로 복구. 상태 clean 재확인.
3. Saved/Revvy/PR160Worktree 별도 sparse worktree에서 dev 병합. 유일한 충돌은 BP_MDPlayerCharacter.
4. 최신 dev BP는 Saved/Revvy/PR160Merge_20260918/BP_MDPlayerCharacter_dev.uasset에 백업. SHA256 8D9376C32089348E5EE222895FD5F7B21B292C98E2D8B1383B27410C42CF6389.
5. 최신 dev BP에서 몸통 SkeletalMeshAsset, AnimClass, BarnacleScrapeMontage, FoamAimMontage만 MK4로 재적용.
6. CDO 속성 전체, 모든 컴포넌트 속성, EventGraph 노드/연결 전후 비교. 네 참조와 메시 별칭만 변경. CollisionCylinder.ShapeBodySetup은 엔진 헤더에서 transient/duplicatetransient임을 확인했으며 컴파일 캐시 초기화는 별도 분류. 지지대 컴포넌트/설정 및 그래프 보존.
7. BP/AnimBP 컴파일 오류0/경고0. L_MerchantTest PIE 시작 및 플레이어/지지대 생성 확인.
8. 실제 선택 데이터가 구형 모델/ABP/팔을 재적용하는 현상을 확인. 영구 roster는 수정하지 않음. 런타임에만 MK4를 지정한 분리 시험에서는 LeftHandIKAlpha1, AimPitch42, 그립오차2.25e-14cm, 상완14.2225/전완11.6795cm, 왼팔꿈치153.06도. 초기 AnimClass 런타임 교체 시 기존 DeltaTime divide-by-zero 경고1회; 컴파일 경고와는 별개.
9. PIE 종료. 전체 어빌리티/멀티플레이/패키징 미검증. Draft 유지: 충돌 해결과 선택 데이터 통합 완료를 혼동하지 말 것.

증거: pr160_merge_bp_result.json, pr160_pie_spawn.json, pr160_pie_probe_result.json. 시험 스크립트는 해당 세션의 전후 메모리 스냅샷을 사용하므로 독립 재실행용이 아님.
