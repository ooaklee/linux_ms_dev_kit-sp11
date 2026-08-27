# Source attribution and test scope

The function extracts and derivative test harnesses in this directory come from
`drivers/media/i2c/ccs/ccs-reg-access.c` at kernel commit
`4c7cf3e7d96359b02eb7c2780dd32bda5927a6f5`. The guarded function is the exact
function committed as `e6fa5cb158b56330b41dda6eb299041b3a3d39f3`.

Original source license: **GPL-2.0-only**. Original source notices:

- Copyright (C) 2020 Intel Corporation.
- Copyright (C) 2011--2012 Nokia Corporation.
- Contact: Sakari Ailus <sakari.ailus@linux.intel.com>.

These notices and license also apply to the copied function code in the harnesses.
They are retained here without altering the byte-exact test inputs recorded by
`provenance.json`. The original source file's SPDX identifier is
`GPL-2.0-only`; the kernel repository includes the corresponding license text.

The harnesses stub device logging and test only the converter, not kernel I2C,
firmware loading, sensor probe or camera operation. The `original_convert`
function is called only for inputs whose original shifts are defined. Undefined
small inputs are instead checked against the required zero underflow result.

Native ARM64 build/run example (with a GCC toolchain supporting UBSan):

```sh
gcc -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=undefined \
  -fno-sanitize-recover=undefined ccs-float-candidate-harness.c -o candidate
./candidate
```

The baseline harness takes one hexadecimal input argument. Values `0x044f3000`,
`0x00000001` and `0x1f800000` intentionally fail UBSan before the fix. Do not treat
these expected failures as a successful hardware test.

The Karsies reference motivated inspection of a reported raw limit word; no
sensor parameter, calibration or clock value was adopted from it. Exact diff
and mail artifacts retain their format-required whitespace. Container build
paths were replaced in selected logs as recorded in `provenance.json`; original
bytes and compiled object files remain in the private validation archive.
