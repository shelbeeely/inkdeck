#!/usr/bin/env bash
# Host tests for InkDeck's pure modules (no Arduino, no SD, no radio).
# Compiles each with g++ against src/ and the SDK's JsonSax, runs it, and
# fails if it exits non-zero or prints FAIL. Usage: test/host/run.sh
set -u
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/../.." && pwd)"
src="$root/src"
json="$root/freeink-sdk/libs/network/JsonSax"
out="$(mktemp -d)"
trap 'rm -rf "$out"' EXIT
cxx="${CXX:-g++} -std=c++17 -O1 -Wall -I$src -I$json/include"
failed=0

run() {  # name, sources..., -- args...
  local name="$1"; shift
  local srcs=() args=()
  while [ $# -gt 0 ] && [ "$1" != "--" ]; do srcs+=("$1"); shift; done
  [ $# -gt 0 ] && shift
  args=("$@")
  if ! $cxx "${srcs[@]}" -o "$out/$name" 2>"$out/$name.log"; then
    echo "BUILD FAIL $name"; cat "$out/$name.log"; failed=1; return
  fi
  local res; res="$("$out/$name" "${args[@]}" 2>&1)"; local rc=$?
  if [ $rc -ne 0 ] || grep -q "FAIL" <<<"$res"; then
    echo "FAIL $name"; echo "$res"; failed=1
  else
    echo "ok   $name"
  fi
}

run journalmeta "$here/journalmeta_test.cpp" "$src/JournalMeta.cpp" -- "$out/doc1.md" "$out/doc2.md"
run enrich "$here/enrich_test.cpp" "$src/JournalMeta.cpp" -- "$out/enriched.md"
run openmeteo "$here/openmeteo_test.cpp" "$src/OpenMeteo.cpp" "$json"/src/*.cpp -- "$here/fixtures"
run usage "$here/usage_test.cpp" "$src/UsageStats.cpp" "$src/JournalMeta.cpp"
run companion "$here/companion_test.cpp" "$src/Companion.cpp"
run linkrewrite "$here/linkrewrite_test.cpp" "$src/LinkRewrite.cpp"
run syncutil "$here/syncutil_test.cpp" "$src/SyncUtil.cpp" -- "hello world"
run repourl "$here/url_test.cpp" "$src/SyncUtil.cpp"

# Tree listing: stream a real GitHub tree response in random chunks.
if $cxx "$here/tree_test.cpp" "$src/TreeListing.cpp" "$src/SyncUtil.cpp" "$json"/src/*.cpp -o "$out/tree" 2>"$out/tree.log"; then
  if "$out/tree" "$here/fixtures/tree.json" 2>/dev/null | diff -q - "$here/fixtures/tree.expected.txt" >/dev/null; then
    echo "ok   tree"
  else echo "FAIL tree"; failed=1; fi
else echo "BUILD FAIL tree"; cat "$out/tree.log"; failed=1; fi

# The enriched entry must still be valid YAML (needs PyYAML).
if python3 -c "import yaml" 2>/dev/null; then
  for f in doc1.md doc2.md enriched.md; do
    [ -f "$out/$f" ] || continue
    if python3 -c "import sys,yaml; yaml.safe_load(open(sys.argv[1]).read().split('---')[1])" "$out/$f"; then
      echo "ok   yaml $f"
    else echo "FAIL yaml $f"; failed=1; fi
  done
fi

# Server (Python): stats, gamification, pet.
if [ -d "$root/server" ] && python3 -c "import yaml" 2>/dev/null; then
  if (cd "$root/server" && python3 -m unittest -q 2>&1 | tail -3); then :; else failed=1; fi
fi

[ $failed -eq 0 ] && echo "ALL HOST TESTS PASSED" || { echo "SOME HOST TESTS FAILED"; exit 1; }
