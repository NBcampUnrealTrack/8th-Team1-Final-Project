#!/usr/bin/env bash
# Multi-User Editing 스크립트 공용 함수 (macOS).
# ifconfig 출력 형식과 ping 의 -t(타임아웃) 의미가 BSD 기준이라 리눅스에서는 안 맞는다.
# Windows 쪽 _Common.ps1 과 같은 일을 한다. 두 파일은 같은 host.json 과
# 같은 Config/DefaultEngine.ini 를 읽는다.
#
# 호스트 주소·포트의 유일한 출처는 DefaultEngine.ini 의 +StaticEndpoints 다.
# host.json 에는 런처만 아는 것(어댑터 매칭, 서브넷, 호스트명)만 둔다.

set -uo pipefail

TOOLS_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$TOOLS_DIR/../.." && pwd)"
PROJECT_FILE="$PROJECT_DIR/MarineDemolition.uproject"
HOST_JSON="$TOOLS_DIR/host.json"

# 엔진 위치는 맥마다 다르다. 아래 순서로 찾고, 못 찾으면 UE_ENGINE_DIR 을 요구한다.
ENGINE_CANDIDATES=(
    "${UE_ENGINE_DIR:-}"
    "$HOME/UnrealEngine"
    "$HOME/dev/UnrealEngine"
    "/Users/Shared/Epic Games/UE_5.8"
    "/Users/Shared/UnrealEngine/UE_5.8"
)

die() {
    echo "" >&2
    for line in "$@"; do echo "$line" >&2; done
    echo "" >&2
    exit 1
}

require_python() {
    if command -v python3 >/dev/null 2>&1; then
        PYTHON=python3
    elif command -v /usr/bin/python3 >/dev/null 2>&1; then
        PYTHON=/usr/bin/python3
    else
        die "[오류] python3 이 없습니다. host.json 을 읽을 수 없습니다." \
            "       맥에서 언리얼을 빌드하려면 Xcode Command Line Tools 가 필요하고," \
            "       거기에 python3 가 포함됩니다:  xcode-select --install"
    fi
}

find_engine() {
    for c in "${ENGINE_CANDIDATES[@]}"; do
        [ -n "$c" ] || continue
        if [ -x "$c/Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor" ]; then
            echo "$c"
            return 0
        fi
    done
    die "[오류] 언리얼 엔진을 찾지 못했습니다." \
        "       다음 위치를 확인했습니다:" \
        "$(printf '         %s\n' "${ENGINE_CANDIDATES[@]}")" \
        "" \
        "       엔진 경로를 직접 지정해서 다시 실행하세요:" \
        "         UE_ENGINE_DIR=/path/to/UnrealEngine $0"
}

# host.json 에서 값을 꺼낸다.  json_get <파이썬표현식>
json_get() {
    "$PYTHON" - "$HOST_JSON" "$1" <<'PY'
import json, sys
cfg = json.load(open(sys.argv[1], encoding="utf-8"))
print(eval(sys.argv[2], {"cfg": cfg}))
PY
}

# 이 맥에서 주어진 서브넷 접두사를 가진 IPv4 주소를 찾는다.
local_address_for() {
    local prefix="$1"
    # BSD ifconfig 는 주소 줄을 탭으로 들여쓴다: "\tinet 100.74.171.37 --> ..."
    ifconfig 2>/dev/null \
        | awk '$1 == "inet" { print $2 }' \
        | grep "^${prefix}" \
        | head -n 1
}

# 주어진 주소가 이 tailnet 의 피어 목록에 있는지 본다.
# 있으면 0, 없거나 판단 불가면 1. (판단 불가일 때 "있다"로 취급해 오진을 피한다)
tailnet_has_peer() {
    local addr="$1"
    local cli=""

    if command -v tailscale >/dev/null 2>&1; then
        cli=tailscale
    elif [ -x /Applications/Tailscale.app/Contents/MacOS/Tailscale ]; then
        cli=/Applications/Tailscale.app/Contents/MacOS/Tailscale
    else
        return 0   # CLI 없음 -> 판단 불가
    fi

    local out
    out="$("$cli" status 2>/dev/null)" || return 0
    [ -z "$out" ] && return 0

    echo "$out" | grep -qF "$addr"
}

# 호스트 주소와 포트의 **유일한 출처**는 Config/DefaultEngine.ini 의 +StaticEndpoints 다.
# 에디터가 실제로 읽는 파일이 그것이라, 런처가 따로 값을 들고 있으면 둘이 어긋났을 때
# 런처는 통과하는데 에디터만 서버를 못 찾는 형태로 나타난다.
# 출력: "주소:포트" 한 줄씩
static_endpoints() {
    local ini="$PROJECT_DIR/Config/DefaultEngine.ini"
    [ -f "$ini" ] || die "[오류] Config/DefaultEngine.ini 를 찾지 못했습니다." "       $ini"

    # 주석(;)으로 시작하는 줄은 패턴이 자연히 걸러낸다.
    grep -E '^[[:space:]]*\+StaticEndpoints[[:space:]]*=' "$ini" 2>/dev/null \
        | sed -E 's/^[^=]*=[[:space:]]*//' \
        | tr -d '\r' \
        | grep -E '^[0-9.]+:[0-9]+$'
}

# host.json 의 transports 를 순서대로 시도해 쓸 수 있는 첫 경로를 고른다.
# 각 경로의 호스트 주소는 DefaultEngine.ini 의 +StaticEndpoints 중 subnetPrefix 가
# 맞는 것을 쓴다. 성공하면 SELECTED_NAME / SELECTED_LOCAL / SELECTED_HOST / SELECTED_PORT.
select_transport() {
    local count
    count="$(json_get 'len(cfg["transports"])')"

    local endpoints
    endpoints="$(static_endpoints)"
    if [ -z "$endpoints" ]; then
        die "[오류] Config/DefaultEngine.ini 에 +StaticEndpoints 가 없습니다." \
            "" \
            "       Tailscale 은 멀티캐스트를 나르지 않아 자동 탐색이 불가능합니다." \
            "       호스트 주소를 직접 지정해야 붙습니다. 예:" \
            "         +StaticEndpoints=100.77.222.84:50002"
    fi

    local failures=()
    local i
    for (( i=0; i<count; i++ )); do
        local name prefix host_addr local_ip endpoint ep
        name="$(json_get "cfg['transports'][$i]['name']")"
        prefix="$(json_get "cfg['transports'][$i]['subnetPrefix']")"

        printf '  %-10s 확인 중...' "$name"

        # 접두사는 줄 맨 앞에서만 맞아야 한다. grep -F 로는 10.0.100.5 같은 주소도
        # "100." 에 걸리므로 셸 패턴으로 앞자리를 고정한다.
        endpoint=""
        while IFS= read -r ep; do
            case "$ep" in "$prefix"*) endpoint="$ep"; break ;; esac
        done <<< "$endpoints"

        if [ -z "$endpoint" ]; then
            echo " 엔드포인트 없음"
            failures+=("  - $name: DefaultEngine.ini 에 ${prefix}* 로 시작하는 +StaticEndpoints 가 없음")
            continue
        fi
        host_addr="${endpoint%%:*}"
        SELECTED_PORT="${endpoint##*:}"

        local_ip="$(local_address_for "$prefix")"
        if [ -z "$local_ip" ]; then
            echo " 인터페이스 없음"
            if [ "$name" = "radmin" ]; then
                failures+=("  - $name: macOS 에는 Radmin VPN 이 없습니다 (Windows 전용). 정상입니다.")
            else
                failures+=("  - $name: ${prefix}* 주소를 가진 인터페이스가 없음")
            fi
            continue
        fi

        if ! ping -c 1 -t 2 "$host_addr" >/dev/null 2>&1; then
            echo " 호스트 $host_addr 응답 없음"

            # 흔한 원인: 호스트가 아예 다른 tailnet(다른 계정)에 로그인되어 있다.
            # 그러면 ping 이 물리 네트워크로 새어나가 TTL 만료로 죽는다.
            if tailnet_has_peer "$host_addr"; then
                failures+=("  - $name: 내 주소는 $local_ip 인데 호스트 $host_addr 에 닿지 않음")
            else
                failures+=("  - $name: 호스트 $host_addr 가 이 tailnet 에 없습니다.")
                failures+=("      tailscale status 목록에 안 보입니다 — 호스트가 다른 계정/tailnet 에")
                failures+=("      로그인되어 있을 수 있습니다. 같은 tailnet 으로 맞춰야 합니다.")
            fi
            continue
        fi

        echo " OK ($local_ip)"
        SELECTED_NAME="$name"
        SELECTED_LOCAL="$local_ip"
        SELECTED_HOST="$host_addr"
        return 0
    done

    die "[오류] 사용할 수 있는 VPN 경로가 없습니다." \
        "" \
        "${failures[@]}" \
        "" \
        "       확인 순서:" \
        "         1) tailscale status  - 호스트가 목록에 보이는가" \
        "         2) 안 보이면 호스트와 내가 같은 tailnet(같은 계정)인지 확인" \
        "         3) 호스트 주소가 바뀌었다면 Config/DefaultEngine.ini 의" \
        "            +StaticEndpoints 를 고쳐 커밋 (여기가 유일한 출처)"
}

# [제거됨 - 2026-09-15] write_multiuser_config
#
# Saved/Config/MacEditor/Engine.ini 에 StaticEndpoints 를 주입하려던 함수였다.
# 동작하지 않는다 — 에디터가 시작하면서 그 파일을 통째로 덮어써서, 써넣은 값이
# 읽히기 전에 사라진다. Config/DefaultEngine.ini 의 +StaticEndpoints 가 유일한 출처다.
