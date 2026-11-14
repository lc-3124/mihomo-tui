#!/usr/bin/env bash
# 渲染固定尺寸的一帧，与 golden 对比。任何像素级变化都会在这里失败。
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="$ROOT/build/mihomo-tui"
fail=0

for spec in 80x24 120x40 160x40 60x20; do
    cols="${spec%x*}"
    rows="${spec#*x}"
    golden="$ROOT/test/golden/demo-$spec.txt"
    tmp="$(mktemp)"

    "$BIN" --snapshot "$tmp" --cols "$cols" --rows "$rows"
    if ! diff -u "$golden" "$tmp"; then
        echo "  FAIL snapshot $spec"
        fail=1
    else
        echo "  ok snapshot $spec"
    fi
    rm -f "$tmp"
done

# "-" 走 stdout，且与写文件的内容一致。
tmp="$(mktemp)"
"$BIN" --snapshot - --cols 80 --rows 24 > "$tmp"
if ! diff -q "$ROOT/test/golden/demo-80x24.txt" "$tmp" >/dev/null; then
    echo "  FAIL snapshot stdout"
    fail=1
else
    echo "  ok snapshot stdout"
fi
rm -f "$tmp"

# --ansi 必须真的带 SGR。
ansi="$(mktemp)"
"$BIN" --snapshot "$ansi" --cols 80 --rows 24 --ansi
if ! grep -q $'\033\[' "$ansi"; then
    echo "  FAIL snapshot --ansi (no SGR found)"
    fail=1
else
    echo "  ok snapshot --ansi"
fi
rm -f "$ansi"

exit "$fail"
