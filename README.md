# ProjectATM

ATM 팀 프로젝트 저장소.

## 시작하기 전에 — Git LFS

이 저장소는 Unreal 바이너리 에셋(`.uasset`, `.umap` 등)을 **Git LFS**로 관리합니다.
`.gitattributes`가 이미 커밋되어 있으므로, 클론하기 전에 LFS만 준비하면 됩니다.

- **Reverb 데스크톱 앱을 쓰는 경우**: 별도 설치가 필요 없습니다. 앱이 git과 git-lfs를 자체 번들로 들고 있습니다.
- **터미널/Fork 등 다른 도구를 쓰는 경우**: `git lfs install`을 한 번 실행해 주세요.

바이너리를 넣기 전에 `.gitattributes`가 먼저 적용되어 있어야 합니다. 이미 커밋되어 있으니
클론 직후부터 바로 유효합니다.

## LFS Guard

`.github/workflows/lfs-guard.yml`이 push/PR마다 검사해서, LFS 포인터가 아닌 **원시 바이너리**가
섞여 들어오면 CI를 실패시킵니다. 클라이언트 종류와 무관하게 서버에서 막습니다.
