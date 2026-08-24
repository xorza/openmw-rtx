#!/bin/bash -ex

set -o pipefail

# The tree is formatted by exactly this major, named here and in .clang-format. clang-format's
# output moves between majors — a pure-virtual whose signature wraps puts `= 0` on its own line
# under 14 and keeps it in place under 15 — so a gate that runs whatever is on PATH answers a
# different question on every machine. Refusing an unexpected major is the answer; producing a
# diff against one nobody in CI runs is not.
REQUIRED_MAJOR=14

CLANG_FORMAT="${CLANG_FORMAT:-clang-format}"

# The wheel carries a real binary and needs no distribution package, which is what makes the gate
# reproducible on a machine whose distribution has moved on.
HOW="pipx install 'clang-format==${REQUIRED_MAJOR}.*'  (then CLANG_FORMAT=~/.local/bin/clang-format)"

if ! command -v "${CLANG_FORMAT:?}" &> /dev/null; then
    echo "${CLANG_FORMAT} is not on PATH; this tree is formatted by clang-format ${REQUIRED_MAJOR}."
    echo "  ${HOW}"
    exit 1
fi

FOUND_MAJOR="$("${CLANG_FORMAT:?}" --version | grep -oP '\d+' | head -1)"

if [[ "${FOUND_MAJOR}" != "${REQUIRED_MAJOR}" ]]; then
    echo "this tree is formatted by clang-format ${REQUIRED_MAJOR}; you have ${FOUND_MAJOR}."
    echo "Point CLANG_FORMAT at a ${REQUIRED_MAJOR}.x binary:"
    echo "  ${HOW}"
    exit 1
fi

git ls-files -- ':(exclude)extern/' '*.cpp' '*.hpp' '*.h' |
    xargs -I '{}' -P $(nproc) bash -ec "\"${CLANG_FORMAT:?}\" --dry-run -Werror \"\${0:?}\" &> /dev/null || \"${CLANG_FORMAT:?}\" \"\${0:?}\" | git diff --color=always --no-index \"\${0:?}\" -" '{}' ||
    ( echo "clang-format differences detected"; exit -1 )
