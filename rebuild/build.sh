#!/usr/bin/env bash
# Compile every rebuild/src/*.cpp on its own (no link) and report per-file status.
#   rebuild/build.sh            all files      rebuild/build.sh src/board.cpp   one file
#   rebuild/build.sh --link     compile all + link with a dummy main; lists undefined functions
#   rebuild/build.sh --test [games] [maxTurns]   build tests/sim.cpp against all sources and run it
#   rebuild/build.sh --cli      build tests/drcli.cpp -> rebuild/drcli (used by tools/emu/diff.py)
cd "$(dirname "$0")"
if [[ ${1:-} == --test ]]; then
  shift; g++ -std=c++17 -O1 -g -Iinclude src/*.cpp tests/sim.cpp -o /tmp/dr_sim_$$ || exit 1
  /tmp/dr_sim_$$ "$@"; rc=$?; rm -f /tmp/dr_sim_$$; exit $rc
fi
if [[ ${1:-} == --cli ]]; then
  g++ -std=c++17 -O1 -g -Iinclude src/*.cpp tests/drcli.cpp -o drcli && echo "built rebuild/drcli"; exit $?
fi
if [[ ${1:-} == --link ]]; then
  obj=$(mktemp -d); trap 'rm -rf "$obj"' EXIT
  for f in src/*.cpp; do g++ -std=c++17 -Iinclude -c "$f" -o "$obj/$(basename "$f" .cpp).o" || exit 1; done
  echo 'int main(){return 0;}' > "$obj/main.cpp"; g++ -c "$obj/main.cpp" -o "$obj/main.o"
  if out=$(g++ "$obj"/*.o -o "$obj/a.out" 2>&1); then echo "LINK OK"; exit 0; fi
  echo "LINK FAILED — undefined:"
  grep -o "undefined reference to \`[^']*'" <<<"$out" | sed "s/undefined reference to \`//; s/'$//" | sort -u | c++filt | sed 's/^/  /'
  exit 1
fi
files=("$@"); [[ ${#files[@]} -eq 0 ]] && files=(src/*.cpp)
ok=0; bad=0
for f in "${files[@]}"; do
  if out=$(g++ -std=c++17 -Wall -Wextra -Iinclude -c "$f" -o /dev/null 2>&1); then echo "OK    $f"; ok=$((ok+1))
  else echo "FAIL  $f"; echo "$out" | grep -E "error" | head -8 | sed 's/^/      /'; bad=$((bad+1)); fi
done
echo "--- $ok ok, $bad failed"
[[ $bad -eq 0 ]]
