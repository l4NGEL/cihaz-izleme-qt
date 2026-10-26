#!/usr/bin/env bash
# Derleme + tüm testler, CI ile aynı imajda. SocketCAN testi için vcan0 gerekir:
#   docker build -t cihaz-izleme-qt:dev .
#   docker run --rm --cap-add NET_ADMIN -v "$PWD:/src" cihaz-izleme-qt:dev scripts/docker-test.sh
set -euo pipefail
cd "$(dirname "$0")/.."

if ! ip link show vcan0 >/dev/null 2>&1; then
    ip link add dev vcan0 type vcan 2>/dev/null && ip link set up vcan0 \
        || echo "uyarı: vcan0 kurulamadı, CAN senaryosu atlanacak"
fi

BUILD=${BUILD:-build}
cmake -S . -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCIHAZ_WERROR=ON "$@"
cmake --build "$BUILD"
ctest --test-dir "$BUILD" --output-on-failure
