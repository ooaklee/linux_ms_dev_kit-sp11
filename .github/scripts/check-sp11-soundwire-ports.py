#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Fork-only qualification of the Qualcomm SoundWire port boundary.

This harness is not part of an upstream kernel patch. It compiles the actual
port-selection/allocation block and unchanged Denali merge helpers, with bounded
host stubs instead of a SoundWire controller. The 32/64-bit cases model bitmap
word widths; they are not separate 32-bit kernel builds or hardware tests.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile


def extract_function(source, name):
    """Extract one complete, uniquely named simple C helper, failing on drift."""
    pattern = re.compile(r"^static bool " + re.escape(name) + r"\(", re.MULTILINE)
    matches = list(pattern.finditer(source))
    if len(matches) != 1:
        raise ValueError("helper declaration is not unique: " + name)
    start = matches[0].start()
    body = source.index("{", start)
    depth = 0
    for index in range(body, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[start:index + 1]
    raise ValueError("unterminated helper: " + name)


def extract_allocation(source):
    """Retain the actual selector, CPS merge, bitmap write and allocation state."""
    function_start = "static int qcom_swrm_stream_alloc_ports("
    function_end = "\nstatic int qcom_swrm_hw_params("
    if source.count(function_start) != 1 or source.count(function_end) != 1:
        raise ValueError("allocator function markers are not unique")
    source = source[source.index(function_start):source.index(function_end)]
    start_marker = "\t\t\t\tm_port = slave->m_port_map[p_rt->num];"
    end_marker = "\t\t\t\tnports++;"
    if source.count(start_marker) != 1 or source.count(end_marker) != 1:
        raise ValueError("allocation block markers are not unique")
    start = source.index(start_marker)
    end = source.index(end_marker, start) + len(end_marker)
    block = source[start:end]
    for marker in ("find_first_zero_bit(", "qcom_swrm_merge_port(", "set_bit("):
        if block.count(marker) != 1:
            raise ValueError("allocation block drifted: " + marker)
    return block


C_TEMPLATE = r'''
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>

#if MODEL_BITS == 32
typedef uint32_t bitmap_word;
#elif MODEL_BITS == 64
typedef uint64_t bitmap_word;
#else
#error unsupported modelled word width
#endif

#define MASTER_ID_WSA 1
#define SWRM_DENALI_VI_DAI 9
#define SWRM_DENALI_CPS_DAI 10

struct qcom_swrm_ctrl {
    struct { int controller_id; } bus;
    void *dev;
};
struct sdw_port_config { unsigned int num, ch_mask; };
struct sdw_port_runtime { unsigned int num, ch_mask; };
struct sdw_slave { unsigned int m_port_map[1]; };

static bool denali_machine;
static unsigned int invalid_bitmap_accesses, error_messages;

static bool of_machine_is_compatible(const char *name)
{
    return denali_machine && strcmp(name, "microsoft,denali") == 0;
}

static void record_error(void *dev, const char *message)
{
    (void)dev;
    if (strcmp(message, "All ports busy\n") != 0)
        abort();
    error_messages++;
}
#define dev_err(dev, message) record_error(dev, message)

static int find_first_zero_bit(const bitmap_word *mask, int count)
{
    if (count < 0 || count > MODEL_BITS)
        abort();
    for (int bit = 0; bit < count; bit++)
        if (!(*mask & ((bitmap_word)1 << bit)))
            return bit;
    return count;
}

static void set_bit(int bit, bitmap_word *mask)
{
    /* Observe a bad narrowed index without dereferencing an invalid address. */
    if (bit < 0 || bit >= MODEL_BITS) {
        invalid_bitmap_accesses++;
        return;
    }
    *mask |= (bitmap_word)1 << bit;
}

/* The unchanged merge helper compares unsigned num with its signed port arg. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-compare"
/*HELPERS*/
#pragma GCC diagnostic pop

static int allocate(struct qcom_swrm_ctrl *ctrl, int maxport,
                    const unsigned int *mapped, int count, int dai_id,
                    bitmap_word *port_mask, struct sdw_port_config *pconfig,
                    int *allocated)
{
    int pn, nports = 0;
    unsigned int m_port;
    for (int index = 0; index < count; index++) {
        struct sdw_slave slave_value = { .m_port_map = { mapped[index] } };
        struct sdw_slave *slave = &slave_value;
        struct sdw_port_runtime runtime = { .num = 0, .ch_mask = 1U << index };
        struct sdw_port_runtime *p_rt = &runtime;
        /*ALLOCATION*/
    }
    *allocated = nports;
    return 0;
}

struct test_case {
    const char *name;
    int maximum;
    unsigned int mapped[2];
    int count, dai_id;
    bool denali, wsa;
    uint64_t initial;
    int expected_result, expected_count;
    unsigned int final_port, final_channels;
};

int main(void)
{
    const int last = MODEL_BITS - 1;
    const struct test_case tests[] = {
        {"mapped-first", 13, {1}, 1, 0, false, true, 1, 0, 1, 1, 1},
        {"mapped-small-bound", 1, {1}, 1, 0, false, true, 1, 0, 1, 1, 1},
        {"mapped-penultimate", 13, {12}, 1, 0, false, true, 1, 0, 1, 12, 1},
        {"mapped-inclusive-last", 13, {13}, 1, 0, false, true, 1, 0, 1, 13, 1},
        {"mapped-above-last", 13, {14}, 1, 0, false, true, 1, -EBUSY, 0, 0, 0},
        {"mapped-unsigned-maximum", 13, {UINT_MAX}, 1, 0, false, true, 1, -EBUSY, 0, 0, 0},
        {"mapped-above-signed-maximum", 13, {(unsigned int)INT_MAX + 1U}, 1, 0, false, true, 1, -EBUSY, 0, 0, 0},
        {"automatic-first-free", 13, {0}, 1, 0, false, true, 1, 0, 1, 1, 1},
        {"automatic-last-in-search", 13, {0}, 1, 0, false, true, 0xfff, 0, 1, 12, 1},
        {"automatic-sentinel-not-port", 13, {0}, 1, 0, false, true, 0x1fff, -EBUSY, 0, 0, 0},
        {"automatic-fully-occupied", 13, {0}, 1, 0, false, true, 0x3fff, -EBUSY, 0, 0, 0},
        {"automatic-small-bound", 1, {0}, 1, 0, false, true, 1, -EBUSY, 0, 0, 0},
        {"modelled-word-last", last, {(unsigned int)last}, 1, 0, false, true, 1, 0, 1, (unsigned int)last, 1},
        {"modelled-word-out-of-range", last, {(unsigned int)last + 1U}, 1, 0, false, true, 1, -EBUSY, 0, 0, 0},
        {"denali-cps-shared-last", 13, {13, 13}, 2, SWRM_DENALI_CPS_DAI, true, true, 1, 0, 1, 13, 3},
        {"denali-vi-distinct", 13, {10, 11}, 2, SWRM_DENALI_VI_DAI, true, true, 1, 0, 2, 11, 2},
        {"other-machine-no-cps-merge", 13, {13, 13}, 2, SWRM_DENALI_CPS_DAI, false, true, 1, 0, 2, 13, 2},
        {"other-controller-no-cps-merge", 13, {13, 13}, 2, SWRM_DENALI_CPS_DAI, true, false, 1, 0, 2, 13, 2},
        {"ordinary-dai-no-cps-merge", 13, {13, 13}, 2, 0, true, true, 1, 0, 2, 13, 2},
    };
    int failures = 0;
    for (unsigned int index = 0; index < sizeof(tests) / sizeof(tests[0]); index++) {
        const struct test_case *test = &tests[index];
        struct qcom_swrm_ctrl ctrl = { .bus.controller_id = test->wsa ? MASTER_ID_WSA : MASTER_ID_WSA + 1 };
        struct sdw_port_config config[64] = {0};
        struct sdw_port_config before[64] = {0};
        bitmap_word mask = (bitmap_word)test->initial, original = mask;
        int allocated = 0;
        invalid_bitmap_accesses = error_messages = 0;
        denali_machine = test->denali;
        int result = allocate(&ctrl, test->maximum, test->mapped, test->count,
                              test->dai_id, &mask, config, &allocated);
        bool passed = result == test->expected_result && invalid_bitmap_accesses == 0 &&
                      allocated == test->expected_count && (mask & 1) != 0;
        if (test->expected_result == 0) {
            bitmap_word expected_mask = original;
            for (int port = 0; port < allocated; port++) {
                if (config[port].num >= MODEL_BITS)
                    abort();
                expected_mask |= (bitmap_word)1 << config[port].num;
            }
            passed = passed && error_messages == 0 && allocated > 0 &&
                     mask == expected_mask &&
                     config[allocated - 1].num == test->final_port &&
                     config[allocated - 1].ch_mask == test->final_channels;
        } else {
            passed = passed && error_messages == 1 && mask == original &&
                     memcmp(config, before, sizeof(config)) == 0;
        }
        printf("%s\t%s\t%d\t%d\t%u\n", test->name, passed ? "pass" : "fail",
               result, allocated, invalid_bitmap_accesses);
        if (!passed)
            failures++;
    }
    return failures ? 1 : 0;
}
'''


def main():
    """Compile and execute the exact source paths in both modelled word widths."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--revision", help="read one immutable Git revision instead of the worktree")
    args = parser.parse_args()
    root = args.source_root.resolve(strict=True)
    if args.revision:
        if not re.fullmatch(r"[0-9a-f]{40}", args.revision):
            raise ValueError("--revision must be a complete lowercase Git object ID")
        content = subprocess.run(
            ["git", "-C", str(root), "show", args.revision + ":drivers/soundwire/qcom.c"],
            check=True, capture_output=True, timeout=30,
        ).stdout
    else:
        content = (root / "drivers/soundwire/qcom.c").read_bytes()
    if not content or len(content) > 256 * 1024:
        raise ValueError("unexpected source size")
    source = content.decode("utf-8", errors="strict")
    allocation = extract_allocation(source)
    helpers = "\n\n".join(extract_function(source, name) for name in (
        "qcom_swrm_is_denali_feedback_dai", "qcom_swrm_merge_port",
    ))
    for name, expected in (("SWRM_DENALI_VI_DAI", "9"), ("SWRM_DENALI_CPS_DAI", "10")):
        values = re.findall(r"^#define\s+" + name + r"\s+(\d+)\s*$", source, re.MULTILINE)
        if values != [expected]:
            raise ValueError("unexpected feedback DAI constant: " + name)
    generated = C_TEMPLATE.replace("/*HELPERS*/", helpers).replace("/*ALLOCATION*/", allocation)
    compiler = shlex.split(os.environ.get("CC", "clang"))
    if not compiler:
        raise ValueError("CC must name a compiler")
    version = subprocess.run(compiler + ["--version"], check=True, capture_output=True, text=True, timeout=15).stdout.splitlines()[0]
    results = []
    with tempfile.TemporaryDirectory(prefix="sp11-soundwire-port-check-") as temporary:
        c_path = Path(temporary) / "port-check.c"
        c_path.write_text(generated, encoding="utf-8")
        for bits in (32, 64):
            executable = Path(temporary) / ("port-check-" + str(bits))
            command = compiler + ["-std=c11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
                                  "-fsanitize=undefined", "-fno-sanitize-recover=all",
                                  "-DMODEL_BITS=" + str(bits), str(c_path), "-o", str(executable)]
            compilation = subprocess.run(command, capture_output=True, text=True, timeout=60)
            if compilation.returncode != 0:
                raise RuntimeError("host C compilation failed:\n" + compilation.stderr)
            run = subprocess.run([str(executable)], capture_output=True, text=True, timeout=15)
            rows = []
            for line in run.stdout.splitlines():
                name, status, result, allocated, invalid = line.split("\t")
                rows.append({"name": name, "status": status, "result": int(result),
                             "allocated": int(allocated), "invalid_bitmap_accesses": int(invalid)})
            if len(rows) != 19 or len({row["name"] for row in rows}) != 19:
                raise ValueError("native harness omitted or repeated cases")
            passed = run.returncode == 0 and not run.stderr and all(row["status"] == "pass" for row in rows)
            results.append({"modelled_bitmap_bits": bits, "passed": passed,
                            "exit_code": run.returncode, "stderr": run.stderr, "cases": rows})
    passed = all(result["passed"] for result in results)
    print(json.dumps({"passed": passed, "revision": args.revision,
                      "source_sha256": hashlib.sha256(content).hexdigest(),
                      "allocation_sha256": hashlib.sha256(allocation.encode()).hexdigest(),
                      "helpers_sha256": hashlib.sha256(helpers.encode()).hexdigest(),
                      "generated_c_sha256": hashlib.sha256(generated.encode()).hexdigest(),
                      "compiler": version, "models": results}, indent=2))
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
