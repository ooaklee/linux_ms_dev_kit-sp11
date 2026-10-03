#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only

set -euo pipefail

readonly SP11_BASE_COMMIT="e57ec2987d4540fa89280d349f79c4d9bcd7cf27"
# This exact checkpoint imports the rebased upstream while retaining the
# original SP11 history. Its tree was reconstructed from the 32 merged SP11
# commits plus the existing Denali EC-reset GPIO reservation. No held topic
# is included. Only this immutable merge may bypass the linear-topic rule;
# its complete SP11 delta remains covered by the integration checks above.
readonly SP11_REFRESH_COMMIT="eea05165923210e93de26484b040f3f3b398b435"
readonly SP11_PREVIOUS_BETA_COMMIT="bf631f9a13b3f6bd622645ba95165c8098932c95"
readonly SP11_RANGE="${SP11_BASE_COMMIT}...HEAD"

die() {
	printf 'error: %s\n' "$*" >&2
	exit 1
}

reviewed_private_defaults_warning() {
	local commit="$1" output="$2" diagnostics patch_id blob_id

	# The Denali producer patch mutates a private defaults copy before regmap
	# initialization. CONST_STRUCT is inapplicable to that writable argument.
	# Pin both the patch and exact resulting source. Stable patch IDs ignore
	# whitespace, including meaningful whitespace inside string literals.
	# Accept exactly this one diagnostic; other findings still fail closed.
	diagnostics="$(grep -E '^(ERROR|WARNING|CHECK):' <<<"$output" || true)"
	[[ "$diagnostics" == \
		'WARNING:CONST_STRUCT: struct reg_default should normally be const' ]] ||
		return 1
	blob_id="$(git rev-parse --verify \
		"${commit}:sound/soc/codecs/lpass-wsa-macro.c")" || return 1
	[[ "$blob_id" == af26d75c4017cb672722bb9e742b7c7fa24ec57e ]] || return 1
	patch_id="$(git diff --no-ext-diff "${commit}^" "$commit" -- |
		git patch-id --stable)" || return 1
	[[ "${patch_id%% *}" == 5a64ac327ac508b199b27da018f14ac2d61ad0f4 ]]
}

git rev-parse --is-inside-work-tree >/dev/null 2>&1 ||
	die "run this check from the repository worktree"

git cat-file -e "${SP11_BASE_COMMIT}^{commit}" 2>/dev/null ||
	die "required Surface Pro 11 base commit is unavailable"

git merge-base --is-ancestor "${SP11_BASE_COMMIT}" HEAD ||
	die "HEAD does not descend from the approved Surface Pro 11 base commit"

git diff --check "${SP11_RANGE}" --

all_changed_paths=()
while IFS= read -r -d '' path; do
	all_changed_paths+=("${path}")
done < <(git diff --name-only -z --diff-filter=ACMRTUXBD "${SP11_RANGE}" --)

if ((${#all_changed_paths[@]} == 0)); then
	printf 'No changes from the approved Surface Pro 11 base commit.\n'
	exit 0
fi

present_paths=()
while IFS= read -r -d '' path; do
	present_paths+=("${path}")
done < <(git diff --name-only -z --diff-filter=ACMRTUXB "${SP11_RANGE}" --)

artifact_path_pattern='^(build|out|artifacts?|dist)/|(^|/)(\.config|Module\.symvers|modules\.order|System\.map|vmlinux|Image(\.gz)?)(/|$)|\.(a|bin|bz2|deb|ddeb|dwo|dtb|dtbo|dylib|efi|elf|gz|img|iso|ko|lz4|o|qcow2|rlib|rmeta|rpm|so|tar|tgz|xz|zip|zst)$'
private_file_pattern='(^|/)(\.env|id_rsa|id_ed25519|credentials?\.json)$|\.(p12|pfx)$'

if ((${#present_paths[@]} > 0)); then
	for path in "${present_paths[@]}"; do
		[[ "${path}" =~ ${artifact_path_pattern} ]] &&
			die "generated build or binary artifact is tracked: ${path}"
		[[ "${path}" =~ ${private_file_pattern} ]] &&
			die "possible private credential file is tracked: ${path}"
	done
fi

binary_paths="$(
	git diff --numstat --diff-filter=ACMRTUXB "${SP11_RANGE}" -- |
		awk -F '\t' '$1 == "-" && $2 == "-" { print $3 }'
)"
[[ -z "${binary_paths}" ]] ||
	die "binary changes are not accepted in the integration delta: ${binary_paths}"

added_lines="$(mktemp)"
trap 'rm -f "${added_lines}"' EXIT

git diff --no-ext-diff --unified=0 "${SP11_RANGE}" -- |
	awk '/^\+\+\+ / { next } /^\+/ { sub(/^\+/, ""); print }' >"${added_lines}"

local_path_pattern='/U'"sers/"'|/ho'"me/"'[[:alnum:]_.-]+/'
tunnel_pattern='ng'"rok"'|ts'"sh"'[[:space:]]'
private_key_pattern='-----BEGIN [A-Z0-9 ]*PRIV'"ATE KEY-----"
token_pattern='gh[pousr]_[A-Za-z0-9_]{20,}|AKIA[0-9A-Z]{16}'
fine_grained_token_pattern='git'"hub_pat_"'[A-Za-z0-9_]{20,}'
lfs_pointer_pattern='version https://git-'"lfs.github.com/spec/v1"
private_content_pattern="(${local_path_pattern}|${tunnel_pattern}|${private_key_pattern}|${token_pattern}|${fine_grained_token_pattern}|${lfs_pointer_pattern})"

if LC_ALL=C grep -En -- "${private_content_pattern}" "${added_lines}"; then
	die "possible workstation path, private endpoint, or credential in added content"
fi

kernel_changed=false
for path in "${all_changed_paths[@]}"; do
	case "${path}" in
	*.c | *.h | *.S | *.rs | *.dts | *.dtsi | *.patch | \
		arch/*/configs/* | */Kconfig* | Kconfig* | */Makefile | Makefile | \
		Documentation/devicetree/bindings/*.yaml)
		kernel_changed=true
		break
		;;
	esac
done

kernel_pathspecs=(
	':(top,glob)**/*.c'
	':(top,glob)**/*.h'
	':(top,glob)**/*.S'
	':(top,glob)**/*.rs'
	':(top,glob)**/*.dts'
	':(top,glob)**/*.dtsi'
	':(top,glob)**/*.patch'
	':(top,glob)arch/*/configs/**'
	':(top,glob)**/Kconfig*'
	':(top,glob)**/Makefile'
	':(top,glob)Documentation/devicetree/bindings/**/*.yaml'
)

if [[ "${kernel_changed}" == true ]]; then
	[[ -x scripts/checkpatch.pl ]] ||
		die "scripts/checkpatch.pl is required for kernel-source changes"
	# File changes are reviewed, but their generic MAINTAINERS reminder is
	# not a style defect. Some extracted commits preserve a contributor as
	# nominal author while carrying only the submitter's authorized sign-off;
	# provenance for those commits is audited separately in the PR body.
	checkpatch_output="$(
		git diff --no-ext-diff "${SP11_RANGE}" -- "${kernel_pathspecs[@]}" |
			scripts/checkpatch.pl --no-tree --strict --show-types \
				--ignore FILE_PATH_CHANGES,NO_AUTHOR_SIGN_OFF - || true
	)"
	printf '%s\n' "${checkpatch_output}"
	if grep -qE '^ERROR:' <<<"${checkpatch_output}"; then
		die "checkpatch.pl reported ERROR-level findings"
	fi
else
	printf 'No kernel-source changes require checkpatch.pl.\n'
fi

review_base="${SP11_REVIEW_BASE:-}"
review_head="${SP11_REVIEW_HEAD:-}"

if [[ -z "${review_base}" && -z "${review_head}" &&
	"${GITHUB_EVENT_NAME:-}" == "pull_request" ]]; then
	merge_and_parents=()
	read -r -a merge_and_parents <<<"$(git rev-list --parents -n 1 HEAD)"
	((${#merge_and_parents[@]} == 3)) ||
		die "pull-request checkout is not a two-parent synthetic merge"
	review_base="${merge_and_parents[1]}"
	review_head="${merge_and_parents[2]}"
fi

if [[ -n "${review_base}" || -n "${review_head}" ]]; then
	[[ -n "${review_base}" && -n "${review_head}" ]] ||
		die "SP11_REVIEW_BASE and SP11_REVIEW_HEAD must be provided together"

	git cat-file -e "${review_base}^{commit}" 2>/dev/null ||
		die "pull-request base commit is unavailable: ${review_base}"
	git cat-file -e "${review_head}^{commit}" 2>/dev/null ||
		die "pull-request head commit is unavailable: ${review_head}"
	# Topic PRs are deliberately re-lifted onto the current beta tip. This
	# keeps each review delta exact and prevents a green check from masking a
	# stale topic that has never been tested against newly integrated work.
	git merge-base --is-ancestor "${review_base}" "${review_head}" ||
		die "pull-request head must be re-lifted onto the current beta tip"

	review_count=0
	while IFS= read -r commit; do
		commit_and_parents=()
		read -r -a commit_and_parents <<<"$(git rev-list --parents -n 1 "${commit}")"
		if [[ "${commit}" == "${SP11_REFRESH_COMMIT}" ]]; then
			((${#commit_and_parents[@]} == 3)) ||
				die "approved upstream refresh must have two parents"
			[[ "${commit_and_parents[1]}" == "${SP11_PREVIOUS_BETA_COMMIT}" ]] ||
				die "approved upstream refresh has the wrong beta parent"
			[[ "${commit_and_parents[2]}" == "${SP11_BASE_COMMIT}" ]] ||
				die "approved upstream refresh has the wrong upstream parent"
			git merge-base --is-ancestor "${review_base}" \
				"${commit_and_parents[1]}" ||
				die "approved upstream refresh does not retain the review base"
			review_count=$((review_count + 1))
			printf 'Approved upstream refresh %s is checked as an integration delta.\n' \
				"${commit}"
			continue
		fi
		((${#commit_and_parents[@]} == 2)) ||
			die "topic range must be linear; merge commit found: ${commit}"

		parent="${commit_and_parents[1]}"
		review_count=$((review_count + 1))
		if git diff --quiet "${parent}" "${commit}" -- "${kernel_pathspecs[@]}"; then
			continue
		fi

		commit_checkpatch_output="$(
			git diff --no-ext-diff "${parent}" "${commit}" -- \
				"${kernel_pathspecs[@]}" |
				scripts/checkpatch.pl --no-tree --strict --show-types \
					--ignore FILE_PATH_CHANGES,NO_AUTHOR_SIGN_OFF - || true
		)"
		printf '%s\n' "${commit_checkpatch_output}"
		if grep -qE '^(ERROR|WARNING|CHECK):' <<<"${commit_checkpatch_output}"; then
			reviewed_private_defaults_warning "$commit" \
				"$commit_checkpatch_output" ||
				die "per-commit source checkpatch findings in ${commit}"
			printf 'Reviewed private-defaults warning retained for %s.\n' "$commit"
		fi
	# Upstream was rebased; its imported history is not an SP11 topic series.
	# Exclude only the pinned upstream ancestry, never arbitrary merge parents.
	done < <(git rev-list --reverse "${review_base}..${review_head}" \
		"^${SP11_BASE_COMMIT}")

	((review_count > 0)) || die "pull request contains no topic commits"
	printf 'Per-commit source checks passed for %d topic commits.\n' \
		"${review_count}"
fi

printf 'Surface Pro 11 integration checks passed for %s.\n' "$(git rev-parse HEAD)"
