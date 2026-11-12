#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Execute actual media-core stream functions with explicit host service doubles.

No sensor-driver dependency. Build outputs live in a TemporaryDirectory. This is
source-level regression evidence, not a complete kernel, PM, locking or device test.
"""
import argparse
import hashlib
import os
from pathlib import Path
import re
import subprocess
import tempfile

SCRIPT = Path(__file__).resolve()
CORE = Path("drivers/media/v4l2-core/v4l2-subdev.c")
HEADER = Path("include/media/v4l2-subdev.h")
FUNCTIONS = ["call_s_stream", "v4l2_subdev_collect_streams",
             "v4l2_subdev_set_streams_enabled", "v4l2_subdev_enable_streams",
             "v4l2_subdev_disable_streams", "v4l2_subdev_s_stream_helper",
             "v4l2_subdev_is_streaming"]


def extract(text, name):
    match = re.search(r"(?m)^(?:static )?(?:inline )?(?:int|void|bool) " +
                      re.escape(name) + r"\(", text)
    if not match:
        raise ValueError(f"function definition missing: {name}")
    start = text.index("{", match.end())
    tokens = re.compile(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|'
                        r"'(?:\\.|[^'\\])*'|[{}]", re.S)
    depth = 0
    for token in tokens.finditer(text, start):
        if token.group() == "{":
            depth += 1
        elif token.group() == "}":
            depth -= 1
            if not depth:
                return text[match.start():token.end()] + "\n"
    raise ValueError(f"unterminated definition: {name}")


def run(command, cwd):
    return subprocess.run(command, cwd=cwd, text=True, capture_output=True,
                          timeout=60)


def compile_and_run(cc, flags, source, build, name, group=None):
    path = build / f"{name}.c"
    binary = build / name
    path.write_text(source)
    compiled = run([cc, *flags, str(path), "-o", str(binary)], build)
    if compiled.returncode:
        raise RuntimeError(f"{name}: compilation failed\n{compiled.stdout}{compiled.stderr}")
    return run([str(binary), *([group] if group else [])], build)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path, default=SCRIPT.parents[2])
    parser.add_argument("--cc", default=os.environ.get("CC", "cc"))
    parser.add_argument("--sanitizers", action="store_true",
                        default=os.environ.get("V4L2_TEST_SANITIZERS") == "1")
    args = parser.parse_args()
    core_text = (args.source_root / CORE).read_text()
    header = (args.source_root / HEADER).read_text()
    functions = {name: extract(core_text, name) for name in FUNCTIONS}
    start = header.index("#define v4l2_subdev_call(sd, o, f, args...)")
    dispatch_lines = []
    for line in header[start:].splitlines(True):
        dispatch_lines.append(line)
        if not line.rstrip().endswith("\\"):
            break
    dispatch = "".join(dispatch_lines)
    defines = []
    for name in ("V4L2_SUBDEV_FL_STREAMS", "V4L2_SUBDEV_FL_STOP_RETRY"):
        match = re.search(r"(?m)^#define " + name + r"[ \t]+[^\n]+$", header)
        if not match:
            raise ValueError(f"flag definition missing: {name}")
        defines.append(match.group())
    template = SCRIPT.with_name("v4l2-stream-stop-fixture.c").read_text()
    template = template.replace("@FLAGS@", "\n".join(defines))
    template = template.replace("@DISPATCH@", dispatch)
    flags = ["-std=gnu11", "-Wall", "-Wextra", "-Werror", "-pthread", "-O1", "-g"]
    if args.sanitizers:
        flags += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
    wrapper = functions["call_s_stream"]
    original, replacements = re.subn(
        r"#if defined\(CONFIG_VIDEO_V4L2_SUBDEV_API\).*?#endif\n", "",
        wrapper, count=1, flags=re.S)
    if replacements != 1:
        raise ValueError("cannot construct unchanged-wrapper negative control")
    helper_only = wrapper.replace(
        "(sd->flags & V4L2_SUBDEV_FL_STOP_RETRY) &&\n\t\t    ", "")
    if helper_only == wrapper:
        raise ValueError("cannot construct rejected helper-only negative control")
    flag_only = wrapper.replace(
        "(sd->flags & V4L2_SUBDEV_FL_STOP_RETRY) &&\n\t\t    "
        "sd->ops->video->s_stream == v4l2_subdev_s_stream_helper",
        "sd->flags & V4L2_SUBDEV_FL_STOP_RETRY")
    if flag_only == wrapper:
        raise ValueError("cannot construct flag-only negative control")
    broad = original.replace("\tif (!enable && ret < 0) {",
                             "\tif (!enable && ret < 0) {\n\t\treturn ret;")
    clear = wrapper.replace("\t\t\treturn ret;",
                            "\t\t\treturn sd->s_stream_enabled = false, ret;")
    variants = [
        ("unchanged-wrapper", original, "helper-retry", "ASSERT helper stop errno"),
        ("rejected-helper-only", helper_only, "eager-compatibility", "ASSERT eager helper single release"),
        ("flag-only", flag_only, "legacy-compatibility", "ASSERT legacy stop compatibility"),
        ("overbroad-propagation", broad, "legacy-compatibility", "ASSERT legacy stop compatibility"),
        ("cleared-ownership", clear, "helper-retry", "ASSERT legacy enabled state retained"),
    ]
    with tempfile.TemporaryDirectory(prefix="v4l2-stream-stop-") as tmp:
        build = Path(tmp)
        enabled_flags = [*flags, "-DCONFIG_VIDEO_V4L2_SUBDEV_API=1"]
        candidate = template.replace("@SOURCE@", "\n".join(functions.values()))
        result = compile_and_run(args.cc, enabled_flags, candidate, build, "candidate")
        if result.returncode:
            raise RuntimeError(f"candidate failed\n{result.stdout}{result.stderr}")
        print(result.stdout, end="")
        for name, mutant, group, assertion in variants:
            selected = {**functions, "call_s_stream": mutant}
            source = template.replace("@SOURCE@", "\n".join(selected.values()))
            result = compile_and_run(args.cc, enabled_flags, source, build, name, group)
            if result.returncode != 1 or assertion not in result.stderr:
                raise RuntimeError(f"{name}: intended assertion did not reject control\n"
                                   f"exit={result.returncode}\n{result.stdout}{result.stderr}")
            print(f"PASS negative control {name}: {assertion}")

        # No helper declaration, matching the real header's API-disabled shape.
        noapi = r'''
#include <stdbool.h>
#include <stdio.h>
struct device { int unused; };
struct v4l2_subdev;
struct video_ops { int (*s_stream)(struct v4l2_subdev *, int); };
struct ops { struct video_ops *video; };
struct v4l2_subdev { struct device *dev; struct ops *ops; bool s_stream_enabled; };
#define WARN_ON(x) (!!(x))
#define dev_warn(dev, fmt, ...) ((void)(dev))
static void v4l2_subdev_enable_privacy_led(struct v4l2_subdev *s) { (void)s; }
static void v4l2_subdev_disable_privacy_led(struct v4l2_subdev *s) { (void)s; }
@WRAPPER@
static int cb(struct v4l2_subdev *s, int enable) { (void)s; return enable ? 0 : -5; }
int main(void) {
    struct video_ops v = { .s_stream = cb }; struct ops o = { .video = &v };
    struct v4l2_subdev s = { .ops = &o };
    if (call_s_stream(&s, 1) || call_s_stream(&s, 0) || s.s_stream_enabled) return 1;
    puts("PASS subdev-api-disabled legacy behavior"); return 0;
}
'''
        result = compile_and_run(args.cc, flags, noapi.replace("@WRAPPER@", wrapper),
                                 build, "no-subdev-api")
        if result.returncode:
            raise RuntimeError(f"API-disabled test failed\n{result.stdout}{result.stderr}")
        print(result.stdout, end="")
    print(f"PASS core SHA256 {hashlib.sha256(core_text.encode()).hexdigest()}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
