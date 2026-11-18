<!-- SPDX-License-Identifier: GPL-2.0-only -->
# Bounded Qualcomm reset and PS883x regression fixture

This fork-only fixture tests the two focused source fixes split from PR #72:
reset error propagation and PS883x orientation retry / balanced unwind. It does
not include or qualify the other twelve files of the experimental recovery
overlay, a USB4 frontend, physical display recovery, or new kernel packages.

Keep these four files together, for example in `.github/scripts/typec-bounded/`.
Run from the kernel repository root with Python 3 and a GNU11-capable C compiler:

```sh
python3 .github/scripts/typec-bounded/test_typec_bounded.py --source-root .
python3 .github/scripts/typec-bounded/test_typec_bounded.py --source-root . --sanitizers
```

`CC` selects the compiler (default `cc`). The optional `--output-json FILE` saves
the complete result. Temporary C sources and binaries use the normal writable
system temporary directory; neither the repository nor this script directory
needs to be writable. No Docker, target access, package installation or device
operation is performed.

The source root must contain the two patched files and Git object storage for
baseline commit `c8cdaac6675ba41fe76e54760caae5c020752319`. The runner reads exactly
these two baseline blobs using `git show`; it never fetches, checks out, modifies
the index or writes refs. `--base-ref` accepts an exact 40-character lowercase
commit SHA, and defaults to that pinned baseline. Use the default when running
these exact mutations. A future baseline requires deliberate review of the
negative controls and expected assertions.

For shallow CI checkouts, fetch only the pinned baseline before running:

```sh
git fetch --no-tags --depth=1 origin c8cdaac6675ba41fe76e54760caae5c020752319
python3 .github/scripts/typec-bounded/test_typec_bounded.py --source-root . --sanitizers
```

A workflow can use a normal pinned checkout action for the candidate head, then
these two commands. It needs only `contents: read`; do not use privileged pull
request events, secrets, device access or a full-history fetch for this check.
The workflow should watch `drivers/clk/qcom/reset.c`,
`drivers/usb/typec/mux/ps883x.c`, this fixture directory and its own workflow file.

The runner extracts actual reset and retimer functions and compiles them with
fake register, supply, provider and allocation APIs. All 29 cases must complete
normally. Seven deliberately broken source variants must fail the exact
expected named cases and assertion expressions. A compiler failure, crash,
sanitizer diagnostic, unrelated assertion failure or unfinished test run cannot
pass a negative control.

Covered behavior includes assert/deassert/update/readback errors, delays and
register masks, identical orientation retry after a failed write, reset-state
caching, orientation validity, upstream-switch rejection, individual configure
write errors, all six partial regulator-enable failures and recovery, probe
clock/read/registration failures, remove before/after power-up and USB4 fallback
policy. The platform fallback list, policy predicate and mode builder are also
compared byte-for-byte with baseline.

This host fixture models provider/API success and failure. It does not replace
ARM64 Kbuild compilation or establish real MMIO ordering, asynchronous kernel
callback lifetimes, electrical regulator state, physical hotplug, suspend or
restart recovery. Keep the broader source preservation PR open for the twelve
remaining overlay files when publishing these two source fixes separately.
