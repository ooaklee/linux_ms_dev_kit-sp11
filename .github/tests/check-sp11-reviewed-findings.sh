#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only

set -euo pipefail

readonly test_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=../scripts/check-sp11-reviewed-findings.sh
source "${test_dir}/../scripts/check-sp11-reviewed-findings.sh"
readonly reviewed_commit="a5eea054c829115fd549c03a75c1a5344cfb3391"
readonly reviewed_output="$(<"${test_dir}/fixtures/checkpatch-writable-defaults.txt")"
tests=0

reject() {
	local label="$1"
	shift
	if sp11_reviewed_checkpatch_finding "$@"; then
		printf 'FAIL: accepted %s\n' "$label" >&2
		exit 1
	fi
	tests=$((tests + 1))
}

sp11_reviewed_checkpatch_finding "$reviewed_commit" "$reviewed_output" || {
	printf 'FAIL: rejected exact reviewed finding and actual tool inputs\n' >&2
	exit 1
}
tests=$((tests + 1))

readonly normalized_trailing_output="$(printf '%s\n\n\n' "$reviewed_output")"
sp11_reviewed_checkpatch_finding "$reviewed_commit" "$normalized_trailing_output" || {
	printf 'FAIL: caller trailing-newline normalization changed the finding\n' >&2
	exit 1
}
tests=$((tests + 1))

reject 'another commit' '0000000000000000000000000000000000000000' "$reviewed_output"
reject 'shortened commit' 'a5eea054c829' "$reviewed_output"
reject 'empty output' "$reviewed_commit" ''
reject 'different location' "$reviewed_commit" "${reviewed_output/2954/2955}"
reject 'different parameter' "$reviewed_commit" "${reviewed_output/\*defaults/\*another}"
reject 'second warning' "$reviewed_commit" "$reviewed_output"$'\nWARNING:CONST_STRUCT: second finding'
reject 'additional error' "$reviewed_commit" "$reviewed_output"$'\nERROR:TEST: additional error'
reject 'additional check' "$reviewed_commit" "$reviewed_output"$'\nCHECK:TEST: additional check'
reject 'changed severity' "$reviewed_commit" "${reviewed_output/WARNING:CONST_STRUCT/ERROR:CONST_STRUCT}"
reject 'extra output' "$reviewed_commit" "$reviewed_output"$'\nUnexpected extra checker output'
reject 'missing argument' "$reviewed_commit"

# Mock only the selected input's hash, not the diagnostic hash or the other
# inputs. No real source or table is changed by these negative controls.
for mismatch_path in scripts/checkpatch.pl scripts/spelling.txt \
	scripts/const_structs.checkpatch; do
	(
		git() {
			if [[ "$#" == 2 && "$1" == hash-object &&
				"$2" == "$mismatch_path" ]]; then
				printf '%s\n' '0000000000000000000000000000000000000000'
			else
				command git "$@"
			fi
		}
		reject "changed input $mismatch_path" "$reviewed_commit" "$reviewed_output"
	)
	tests=$((tests + 1))
done

(
	git() { return 128; }
	reject 'fingerprint tool failure' "$reviewed_commit" "$reviewed_output"
)
tests=$((tests + 1))

(
	cd .github
	reject 'non-root working directory' "$reviewed_commit" "$reviewed_output"
)
tests=$((tests + 1))

(
	git() {
		if [[ "$#" == 2 && "$1" == hash-object && "$2" == --stdin ]]; then
			printf '%064d\n' 0
		else
			command git "$@"
		fi
	}
	reject 'different object-format fingerprint' "$reviewed_commit" "$reviewed_output"
)
tests=$((tests + 1))

printf 'Reviewed-finding controls passed: %d cases; no tracked source changed.\n' "$tests"
