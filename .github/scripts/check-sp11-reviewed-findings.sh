#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only

# A documented finding is not a type-, file-, or topic-wide warning exemption.
# The caller obtains complete commit IDs from Git and prints the unmodified
# checker output before asking whether this exact finding has a disposition.
sp11_reviewed_checkpatch_finding() {
	local commit output finding_object repository_prefix

	(($# == 2)) || return 1
	commit="$1"
	output="$2"

	# This historical Denali mutator writes a private copy. Its three shared
	# register-default tables are const; making this argument const is invalid.
	# See ../docs/checkpatch-reviewed-findings.md before updating any pin.
	[[ "$commit" == "a5eea054c829115fd549c03a75c1a5344cfb3391" ]] ||
		return 1
	repository_prefix="$(git rev-parse --show-prefix)" || return 1
	[[ -z "$repository_prefix" ]] || return 1

	# The caller's command substitution collapses all trailing newlines. Add
	# one for the reviewed normalized stdout fingerprint; interior text and
	# extra diagnostics are not normalized and still fail.
	finding_object="$(printf '%s\n' "$output" | git hash-object --stdin)" ||
		return 1
	[[ "$finding_object" == "78cbd421d402b4f63db7cee35a9954ea2b527345" ]] ||
		return 1

	# A tool/table change requires another review even if the visible diagnostic
	# happens to remain the same. Missing or modified inputs are not accepted.
	[[ "$(git hash-object scripts/checkpatch.pl)" == \
		"2b7a42bbdd94f827253088ae021571b7183ac662" ]] || return 1
	[[ "$(git hash-object scripts/spelling.txt)" == \
		"3372873cd7bb7dd6c73cf455862cb7de15f03ffc" ]] || return 1
	[[ "$(git hash-object scripts/const_structs.checkpatch)" == \
		"6eb94fddc338a211a2aac9cb6e4cd9d15c4b4a6c" ]] || return 1

	return 0
}
