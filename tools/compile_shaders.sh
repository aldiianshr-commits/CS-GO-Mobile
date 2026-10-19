#!/bin/sh
# Kompilasi ulang shader GLSL -> SPIR-V tertanam (app/src/main/cpp/shaders/shaders_spv.h)
# Pemakaian: GLSLANG=/path/ke/glslang tools/compile_shaders.sh
set -e
G=${GLSLANG:-glslang}
D=$(dirname "$0")/../app/src/main/cpp/shaders
OUT=$D/shaders_spv.h
TMP=$(mktemp -d)
{
  echo "// Dibuat otomatis dari shaders/*.vert|frag dengan glslang (tools/compile_shaders.sh) — jangan diedit"
  echo "#pragma once"
  echo "#include <cstdint>"
} > "$OUT"
for s in world.vert world.frag post.vert post.frag; do
  n=$(echo $s | tr . _)
  "$G" -V --target-env vulkan1.0 --vn spv_$n -o "$TMP/$n.h" "$D/$s" > /dev/null
  grep -v '^[[:space:]]*#' "$TMP/$n.h" | grep -v '^[[:space:]]*//' | sed 's/^const uint32_t/static const uint32_t/' >> "$OUT"
done
rm -rf "$TMP"
echo "OK: $OUT"
