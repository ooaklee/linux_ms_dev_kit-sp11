#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Compile actual kernel functions with fake I/O; reject deliberately broken variants."""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
DEFAULT_BASE = 'c8cdaac6675ba41fe76e54760caae5c020752319'
RESET = Path('drivers/clk/qcom/reset.c')
RETIMER = Path('drivers/usb/typec/mux/ps883x.c')
RESET_NAMES = ['qcom_reset', 'qcom_reset_set_assert', 'qcom_reset_assert', 'qcom_reset_deassert']
RETIMER_NAMES = ['ps883x_should_disable_usb4', 'ps883x_enable_vregs', 'ps883x_disable_vregs',
                 'ps883x_reset', 'ps883x_configure', 'ps883x_set', 'ps883x_sw_set',
                 'ps883x_retimer_set', 'ps883x_get_vregs', 'ps883x_retimer_probe', 'ps883x_retimer_remove']


def extract(source, name):
    match = re.search(r'^static\s+(?:int|void|bool)\s+' + re.escape(name) + r'\s*\(', source, re.M)
    if not match:
        raise ValueError('missing function: ' + name)
    start = source.index('{', match.start())
    depth = 1
    end = start + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[match.start():end]


def block(source, marker):
    start = source.index(marker)
    end = source.index('\n};', start) + 3
    return source[start:end]


def replace_function(source, name, replacement):
    return source.replace(extract(source, name), replacement, 1)


def render(reset, retimer):
    constants = '\n'.join(re.findall(r'^#define\s+.*$', retimer, re.M))
    return '\n\n'.join([
        (HERE / 'fixture-prefix.c').read_text(), constants,
        block(retimer, 'static const char * const ps883x_disable_usb4_compats[]'),
        block(retimer, 'struct ps883x_retimer {'),
        block(retimer, 'static const struct regmap_config ps883x_retimer_regmap ='),
        *(extract(reset, name) for name in RESET_NAMES),
        *(extract(retimer, name) for name in RETIMER_NAMES),
        (HERE / 'fixture-tests.c').read_text(),
    ])


def run_variant(name, reset, retimer, directory, sanitizers):
    source = directory / (name + '.c')
    binary = directory / name
    source.write_text(render(reset, retimer))
    command = [os.environ.get('CC', 'cc'), '-std=gnu11', '-Wall', '-Wextra', '-Werror',
               '-Wno-unused-parameter', '-Wno-unused-function', '-Wno-misleading-indentation',
               '-g', '-O1', str(source), '-o', str(binary)]
    if sanitizers:
        command[1:1] = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError(name + ': fixture did not compile\n' + result.stderr)
    result = subprocess.run([str(binary)], capture_output=True, text=True)
    return {'name': name, 'exit_code': result.returncode, 'stdout': result.stdout, 'stderr': result.stderr}


def git_blob(root, revision, path):
    # Require an exact source identity; no revision syntax or arbitrary Git options.
    if not re.fullmatch(r'[0-9a-f]{40}', revision):
        raise ValueError('--base-ref must be an exact 40-character lowercase commit SHA')
    result = subprocess.run(['git', '--no-pager', '-C', str(root), 'show',
                             revision + ':' + path.as_posix()], capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError('cannot read baseline ' + revision + ':' + path.as_posix() +
                           '; fetch this exact base commit before running the fixture\n' + result.stderr)
    return result.stdout


EXPECTED_FAILURES = {
    'negative-reset-pulse-errors': {
        'reset-assert-error': {'qcom_reset(&rst.rcdev,0)==-EBUSY', '!f.deassert_calls', '!f.delay'},
        'reset-deassert-error': {'qcom_reset(&rst.rcdev,1)==-ETIMEDOUT'},
    },
    'negative-reset-regmap-errors': {
        'reset-write-error': {'qcom_reset_assert(&rst.rcdev,0)==-EIO', '!f.reads'},
        'reset-readback-error': {'qcom_reset_deassert(&rst.rcdev,1)==-ETIMEDOUT'},
    },
    'negative-reset-readback-error': {
        'reset-readback-error': {'qcom_reset_deassert(&rst.rcdev,1)==-ETIMEDOUT'},
    },
    'negative-orientation-cache-before-write': {
        'orientation-retry-after-failure': {'r.orientation==TYPEC_ORIENTATION_NORMAL',
            '!r.orientation_valid', 'f.assigns==2', 'map.reg[0]&CONN_STATUS_0_ORIENTATION_REVERSED'},
        'orientation-invalid-same-value': {'f.assigns==1&&r.orientation_valid'},
    },
    'negative-reset-keeps-orientation-valid': {
        'orientation-reset-invalidation': {'r.in_reset&&!r.orientation_valid'},
    },
    'negative-probe-double-disable': {
        'probe-unwind-2': {'f.supply[i]==n', '!f.bad_disable'},
        'probe-unwind-3': {'f.supply[i]==n', '!f.bad_disable'},
    },
    'negative-remove-double-disable': {
        'probe-unwind-5': {'f.supply[i]==n', '!f.bad_disable'},
    },
}


def check_result(result):
    name = result['name']
    summary = re.fullmatch(r'29 behavioral cases; (\d+) failures\n', result['stdout'])
    if not summary:
        raise RuntimeError(name + ': all 29 cases did not finish normally')
    failures = int(summary.group(1))
    if name == 'candidate':
        if result['exit_code'] != 0 or failures != 0 or result['stderr']:
            raise RuntimeError(name + ': candidate failed\n' + json.dumps(result))
        return
    if result['exit_code'] != 1 or failures <= 0:
        raise RuntimeError(name + ': mutation was not behaviorally rejected')
    observed = {}
    lines = result['stderr'].splitlines()
    for line in lines:
        assertion = re.fullmatch(r'([^:]+):\d+: (.+)', line)
        if not assertion:
            # Sanitizer diagnostics, aborts and unrelated runtime failures cannot pass a negative control.
            raise RuntimeError(name + ': unexpected diagnostic: ' + line)
        observed.setdefault(assertion.group(1), set()).add(assertion.group(2))
    if observed != EXPECTED_FAILURES[name] or len(lines) != failures:
        raise RuntimeError(name + ': wrong case/assertion failed\n' + json.dumps(result))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-root', type=Path, default=Path.cwd(), help='kernel repository root (default: current directory)')
    parser.add_argument('--base-ref', default=DEFAULT_BASE, help='exact baseline commit SHA available in the repository')
    parser.add_argument('--output-json', type=Path)
    parser.add_argument('--sanitizers', action='store_true')
    args = parser.parse_args()
    reset = (args.source_root / RESET).read_text()
    retimer = (args.source_root / RETIMER).read_text()
    old_reset = git_blob(args.source_root, args.base_ref, RESET)
    old_retimer = git_blob(args.source_root, args.base_ref, RETIMER)
    # Preserve 7.3 platform policy as a complete block and mode-building function.
    for marker in ['static const char * const ps883x_disable_usb4_compats[]']:
        assert block(retimer, marker) == block(old_retimer, marker)
    assert extract(retimer, 'ps883x_should_disable_usb4') == extract(old_retimer, 'ps883x_should_disable_usb4')
    assert extract(retimer, 'ps883x_set') == extract(old_retimer, 'ps883x_set')
    variants = [('candidate', reset, retimer)]
    variants += [('negative-reset-pulse-errors', replace_function(reset, 'qcom_reset', extract(old_reset, 'qcom_reset')), retimer)]
    variants += [('negative-reset-regmap-errors', replace_function(reset, 'qcom_reset_set_assert', extract(old_reset, 'qcom_reset_set_assert')), retimer)]
    readback = 'return regmap_read(rst->regmap, map->reg, &mask);'
    assert reset.count(readback) == 1
    variants += [('negative-reset-readback-error', reset.replace(readback, 'regmap_read(rst->regmap, map->reg, &mask);\n\treturn 0;'), retimer)]
    variants += [('negative-orientation-cache-before-write', reset, replace_function(retimer, 'ps883x_sw_set', extract(old_retimer, 'ps883x_sw_set')))]
    invalid = '\tretimer->orientation_valid = false;\n'
    reset_function = extract(retimer, 'ps883x_reset')
    assert reset_function.count(invalid) == 1
    variants += [('negative-reset-keeps-orientation-valid', reset, replace_function(retimer, 'ps883x_reset', reset_function.replace(invalid, '')))]
    variants += [('negative-probe-double-disable', reset, replace_function(retimer, 'ps883x_retimer_probe', extract(old_retimer, 'ps883x_retimer_probe')))]
    variants += [('negative-remove-double-disable', reset, replace_function(retimer, 'ps883x_retimer_remove', extract(old_retimer, 'ps883x_retimer_remove')))]
    results = []
    with tempfile.TemporaryDirectory(prefix='typec-source-fixture-') as temporary:
        directory = Path(temporary)
        for name, rs, ps in variants:
            result = run_variant(name, rs, ps, directory, args.sanitizers)
            results.append(result)
            check_result(result)
            print(name + ': ' + ('PASS' if name == 'candidate' else 'REJECTED') + ' ' + result['stdout'].strip())
    if args.output_json:
        args.output_json.write_text(json.dumps({'sanitizers': args.sanitizers, 'base_ref': args.base_ref,
                                                'source_functions': RESET_NAMES + RETIMER_NAMES,
                                                'results': results}, indent=2) + '\n')


if __name__ == '__main__':
    main()
