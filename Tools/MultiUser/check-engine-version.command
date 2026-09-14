#!/usr/bin/env bash
# 엔진 빌드 지문 출력 (macOS)
#
# 멀티유저 세션 전 체크리스트 1번. 참가자 전원이 이걸 돌려서 "엔진 지문" 줄이
# 글자 그대로 같은지 확인한다. 다르면 접속은 되어도 트랜잭션이 어긋난다.
#
# Windows 는 Check-EngineVersion.bat 을 쓴다. 출력 형식은 동일하다.
#
# 엔진 경로가 특이하면:  UE_ENGINE_DIR=/path/to/UnrealEngine ./check-engine-version.command

source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/_common.sh"

require_python

ENGINE_DIR="$(find_engine)"
VERSION_FILE="$ENGINE_DIR/Engine/Build/Build.version"

[ -f "$VERSION_FILE" ] || die \
    "[오류] Build.version 을 찾지 못했습니다." \
    "       $VERSION_FILE" \
    "" \
    "       엔진 경로를 직접 지정해서 다시 실행하세요:" \
    "         UE_ENGINE_DIR=/path/to/UnrealEngine $0"

# Concert 가 비교하는 값들 (ConcertVersion.h 의 FConcertEngineVersionInfo).
# CompatibleChangelist 는 바이너리 호환성의 기준이다.
read -r FINGERPRINT CHANGELIST PROMOTED <<EOF
$("$PYTHON" - "$VERSION_FILE" <<'PY'
import json, sys
v = json.load(open(sys.argv[1], encoding="utf-8"))
print("{}.{}.{}-{}-L{}-{}".format(
        v.get("MajorVersion"), v.get("MinorVersion"), v.get("PatchVersion"),
        v.get("CompatibleChangelist"), v.get("IsLicenseeVersion"), v.get("BranchName")),
      v.get("Changelist"),
      v.get("IsPromotedBuild"))
PY
)
EOF

HEAD="$(git -C "$ENGINE_DIR" rev-parse --short HEAD 2>/dev/null || true)"

cat <<EOF

============================================================
  엔진 지문 (이 줄이 전원 동일해야 함)

    $FINGERPRINT

============================================================

  엔진 경로       : $ENGINE_DIR
EOF

[ -n "$HEAD" ] && echo "  엔진 git HEAD   : $HEAD"

cat <<EOF
  Changelist      : $CHANGELIST
  IsPromotedBuild : $PROMOTED

  지문은 같은데 Changelist 만 다르면 소스 빌드와 런처 빌드를 섞은 경우다.
  그때는 Multi-User 설정의 Support Mixed Build Types 를 켜야 할 수 있다.

EOF
