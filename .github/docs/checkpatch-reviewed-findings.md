# Reviewed checkpatch finding: writable Denali register defaults

This policy records one semantic false positive. It does not change kernel
source, turn a diagnostic into hardware qualification, or authorize a merge.
The raw checker output remains visible. Every other per-commit `ERROR`,
`WARNING` or `CHECK` remains fatal.

## Exact disposition

Commit: [`a5eea054c829115fd549c03a75c1a5344cfb3391`](https://github.com/ooaklee/linux_ms_dev_kit-sp11/commit/a5eea054c829115fd549c03a75c1a5344cfb3391)

Finding: `WARNING:CONST_STRUCT: struct reg_default should normally be const`
on the `defaults` parameter of `wsa_macro_set_denali_defaults()` in
`sound/soc/codecs/lpass-wsa-macro.c` (line 2954 in that historical commit).

The shared `wsa_defaults`, `wsa_defaults_v2_1` and `wsa_defaults_v2_5` tables
are already `const`. Probe allocates a private register-default array and copies
the appropriate shared tables into it. The Denali-only mutator then adjusts
that copy before regmap construction; it does not write the shared tables or
force hardware register writes.

The mutator contains seven assignments to `defaults[i].def`. Adding `const`
to its parameter would make those assignments invalid. An isolated AArch64 C
typing control using the actual mutator and `reg_default` declaration confirmed
that the writable form passes and the const form rejects all seven assignments.
That control uses stubbed codec context and symbolic register values; it is not
a kernel build, regmap-content comparison or hardware acceptance test.

Preserving this mutable copy is deliberate. Neither changing qualified audio
behaviour nor suppressing `CONST_STRUCT` globally is an acceptable lint fix.

## Fail-closed boundaries

The helper accepts only the conjunction of:

- The complete historical commit ID above, supplied by the validator's Git
  commit enumeration, not a shortened ID or a matching subject.
- The fingerprint of normalized complete checker stdout, including the sole
  warning, location, parameter line, totals and explanatory text.
- The exact content fingerprints of `checkpatch.pl`, `spelling.txt` and
  `const_structs.checkpatch` used for the review.

Fingerprints use `git hash-object`, with the repository's existing object format.
No additional hashing dependency or output-substring matching is introduced.
Changing that object format fails closed; a different-format fingerprint is
covered by a negative control. The caller's shell command substitution removes
all trailing newline characters, and the helper adds one before hashing. Thus
changing only trailing blank lines does not change the fingerprint. Interior
whitespace, diagnostic lines and other text are not normalized. Both validator
and classification require the repository-root working directory.

A changed commit, location, source line, checker/table, second warning, error,
check or altered output fails the normal per-commit gate. A later cherry-pick
with a new commit ID needs an explicit new review. The usual aggregate error,
complete-input, tool-failure, ancestry, private-content and artifact checks are
unchanged. The approved upstream-refresh exception is not broadened.

## Validation and review

Run the helper's positive and negative controls from the repository root:

```sh
bash .github/tests/check-sp11-reviewed-findings.sh
```

Also run the complete validator against the actual qualification history, with
the exact review base and head supplied through `SP11_REVIEW_BASE` and
`SP11_REVIEW_HEAD`. Passing fixture controls alone does not prove integration
coverage. Retain the original failed checker result as historical evidence;
the new policy produces a separate explicitly dispositioned result.

Updating any pin requires reviewing the actual source and complete diagnostic
again, demonstrating why the finding is semantically incorrect, retaining
negative controls, and normal policy review. A file-wide or topic-wide ignore
is not a replacement for that review. Source-hygiene success never substitutes
for contributor certification, hardware qualification or final-composition
acceptance.
