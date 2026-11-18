#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Host regression tests for the actual GPI stale-event loop; no device access.

The fixture uses synthetic coherent addresses, not evidence that a platform can
allocate those addresses. This fork-only test is not part of upstream patches.
"""

import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "drivers/dma/qcom/gpi.c"
NORMALIZE = re.compile(
    r"\n(?P<indent>\t+)if \(!gpii_has_denali_qspi\(gpii\)\)\n"
    r"(?P=indent)\tlocal_rp = lower_32_bits\(local_rp\);"
)


def function(source, name):
    """Extract a complete driver function, failing if its boundary changes."""
    pattern = r"^static [^{;]+\b" + name + r"\([^;]*?\)\n\{.*?^\}"
    matches = re.findall(pattern, source, re.MULTILINE | re.DOTALL)
    if len(matches) != 1:
        raise AssertionError("expected one complete driver function: " + name)
    return matches[0]


HEADER = r"""
#include <inttypes.h>
#include <setjmp.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef uint32_t u32;
typedef uint64_t phys_addr_t;
#define MAX_CHANNELS_PER_GPII 2
#define QCOM_GPI_QSPI 3
#define STALE_EV_TYPE 0xff
#define lower_32_bits(value) ((u32)(value))
#define SLOTS 4

struct gpii;
struct gchan { struct gpii *gpii; u32 chid; int protocol; int pm_state; };
struct gpi_dev { bool is_denali; };
struct gpi_ring {
    void *base, *rp;
    unsigned int len, el_size;
    phys_addr_t phys_addr;
};
union gpi_event {
    struct { u32 chid, type; } xfer_compl_event;
    unsigned char padding[16];
};
struct gpii {
    struct gpi_dev *gpi_dev;
    struct gchan gchan[MAX_CHANNELS_PER_GPII];
    struct gpi_ring ev_ring;
    void *ev_ring_rp_lsb_reg;
};
static struct {
    struct gpi_dev dev;
    struct gpii gpi;
    union gpi_event events[SLOTS];
    u32 registers[2];
    unsigned int low_reads, high_reads;
    bool completed;
    jmp_buf stop;
} fixture;

/* Bound a non-converging mutant without changing the extracted driver loop. */
static u32 readl(void *address)
{
    if (address == &fixture.registers[0]) {
        if (++fixture.low_reads > SLOTS + 1)
            longjmp(fixture.stop, 1);
        return fixture.registers[0];
    }
    if (address == &fixture.registers[1]) {
        fixture.high_reads++;
        return fixture.registers[1];
    }
    longjmp(fixture.stop, 1);
}
"""

CASES = r"""
static int run_case(int mode, int phase, bool high, unsigned int start,
                    unsigned int target, bool mismatch)
{
    phys_addr_t context;
    unsigned int steps = (target + SLOTS - start) % SLOTS;
    bool visited[SLOTS] = { false };
    bool full_width = mode == 3;
    bool ok;

    memset(&fixture, 0, sizeof(fixture));
    fixture.dev.is_denali = mode >= 2;
    fixture.gpi.gpi_dev = &fixture.dev;
    fixture.gpi.gchan[0].gpii = &fixture.gpi;
    fixture.gpi.gchan[0].chid = 0;
    /* Configure QSPI on RX: either configured channel selects full width. */
    fixture.gpi.gchan[1].protocol = (mode & 1) ? QCOM_GPI_QSPI : 0;
    fixture.gpi.gchan[0].pm_state = phase;
    fixture.gpi.gchan[1].pm_state = phase;
    fixture.gpi.ev_ring.base = fixture.events;
    fixture.gpi.ev_ring.rp = &fixture.events[start];
    fixture.gpi.ev_ring.len = sizeof(fixture.events);
    fixture.gpi.ev_ring.el_size = sizeof(fixture.events[0]);
    fixture.gpi.ev_ring.phys_addr = UINT64_C(0x1000) +
                                   (high ? UINT64_C(0x100000000) : 0);
    fixture.gpi.ev_ring_rp_lsb_reg = fixture.registers;
    context = fixture.gpi.ev_ring.phys_addr +
              target * sizeof(fixture.events[0]);
    if (mismatch)
        context += UINT64_C(0x100000000);
    fixture.registers[0] = lower_32_bits(context);
    fixture.registers[1] = context >> 32;
    for (unsigned int i = 0; i < SLOTS; i++) {
        fixture.events[i].xfer_compl_event.chid = i % 2;
        fixture.events[i].xfer_compl_event.type = 0xa5;
    }
    if (setjmp(fixture.stop) == 0) {
        gpi_mark_stale_events(&fixture.gpi.gchan[0]);
        fixture.completed = true;
    }
    ok = fixture.completed == !mismatch;
    if (fixture.completed) {
        ok = ok && fixture.low_reads == steps + 1;
        ok = ok && fixture.high_reads == (full_width ? steps + 1 : 0);
        ok = ok && fixture.gpi.ev_ring.rp == &fixture.events[start];
        for (unsigned int i = 0; i < steps; i++)
            visited[(start + i) % SLOTS] = true;
        for (unsigned int i = 0; i < SLOTS; i++) {
            u32 expected = visited[i] && i % 2 == 0 ? STALE_EV_TYPE : 0xa5;
            ok = ok && fixture.events[i].xfer_compl_event.type == expected;
        }
    }
    if (!ok)
        fprintf(stderr, "FAIL mode%d-%s-start%u-target%u-phase%d-mismatch%d\n",
                mode, high ? "high" : "low", start, target, phase, mismatch);
    return !ok;
}

int main(void)
{
    const unsigned int positions[][2] = {{0, 0}, {0, 1}, {0, 3}, {3, 1}};
    unsigned int failures = 0, cases = 0;

    /* Generic, non-Denali QSPI, Denali non-QSPI, configured Denali QSPI. */
    for (int mode = 0; mode < 4; mode++)
        for (int phase = 0; phase < 3; phase++)
            for (int high = 0; high < 2; high++)
                for (unsigned int i = 0; i < 4; i++) {
                    failures += run_case(mode, phase, high,
                                         positions[i][0], positions[i][1], false);
                    cases++;
                }
    /* Equal low words must not hide a distinct full-width QSPI pointer. */
    for (int phase = 0; phase < 3; phase++) {
        failures += run_case(3, phase, true, 0, 0, true);
        cases++;
    }
    printf("%u cases, %u failures\n", cases, failures);
    return failures ? 1 : 0;
}
"""


class StaleEventTests(unittest.TestCase):
    """Execute the driver functions and detect regressions at either site."""

    @classmethod
    def setUpClass(cls):
        """Read each actual driver function once, without device access."""
        source = SOURCE.read_text()
        cls.helpers = "\n".join(function(source, name) for name in
                                ("gpii_has_denali_qspi", "to_physical", "gpi_read_ev_rp"))
        cls.loop = function(source, "gpi_mark_stale_events")
        if len(NORMALIZE.findall(cls.loop)) != 2:
            raise AssertionError("update mutation controls for changed normalisation sites")

    def run_variant(self, keep):
        """Compile a source-extracted loop or one deliberately broken control."""
        index = 0

        def change(match):
            """Retain only the normalisation sites selected by this variant."""
            nonlocal index
            index += 1
            return match.group(0) if index in keep else ""

        loop = NORMALIZE.sub(change, self.loop)
        with tempfile.TemporaryDirectory(prefix="gpi-stale-test-") as directory:
            root = Path(directory)
            source, binary = root / "test.c", root / "test"
            source.write_text(HEADER + self.helpers + "\n" + loop + CASES)
            flags = ["-std=gnu11", "-Wall", "-Wextra", "-Werror"]
            if os.environ.get("GPI_TEST_SANITIZERS") == "1":
                flags += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
            command = shlex.split(os.environ.get("CC", "cc"))
            subprocess.run(command + flags + [str(source), "-o", str(binary)], check=True)
            return subprocess.run([str(binary)], text=True, capture_output=True, timeout=10)

    def test_current_source(self):
        """Check generic width handling and unchanged configured-QSPI semantics."""
        result = self.run_variant({1, 2})
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("99 cases, 0 failures", result.stdout)

    def test_no_normalisation_control(self):
        """The pre-fix width comparison must fail a high-address empty ring."""
        result = self.run_variant(set())
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertIn("FAIL mode0-high-start0-target0", result.stderr)
        self.assertNotIn("FAIL mode3-", result.stderr)

    def test_initial_only_control(self):
        """Fixing only the initial comparison must fail after advancement."""
        result = self.run_variant({1})
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertIn("FAIL mode0-high-start0-target1", result.stderr)
        self.assertNotIn("FAIL mode3-", result.stderr)

    def test_iteration_only_control(self):
        """Fixing only iterations must incorrectly consume an initially empty ring."""
        result = self.run_variant({2})
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertIn("FAIL mode0-high-start0-target0", result.stderr)
        self.assertNotIn("FAIL mode3-", result.stderr)


if __name__ == "__main__":
    unittest.main(verbosity=2)
