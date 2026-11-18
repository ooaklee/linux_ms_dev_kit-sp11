// SPDX-License-Identifier: GPL-2.0-only
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { mkdtemp, rm, writeFile } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { spawnSync } from 'node:child_process';
import test from 'node:test';
import { fileURLToPath } from 'node:url';
import { WINDOWS_RESOURCE_LIMITS, WindowsResourceDecodeError, decodeCmResourceListArm64, decodeRegistryResources } from './windows-resources.mjs';

// All bytes are independently generated synthetic fixtures, not OEM captures.
const hash = (bytes) => createHash('sha256').update(bytes).digest('hex');
function descriptor(type, flags = 0) {
  const bytes = Buffer.alloc(20);
  bytes[0] = type;
  bytes[1] = 1;
  bytes.writeUInt16LE(flags, 2);
  return bytes;
}
function memory(base = 0x123456789abcdef0n, length = 0x2000) {
  const bytes = descriptor(3);
  bytes.writeBigUInt64LE(base, 4);
  bytes.writeUInt32LE(length, 12);
  bytes.writeUInt32LE(0xabcdef01, 16);
  return bytes;
}
function interrupt(flags = 1) {
  const bytes = descriptor(2, flags);
  bytes.writeUInt32LE(0x10203, 4);
  bytes.writeUInt32LE(711, 8);
  bytes.writeBigUInt64LE(0xffffffffffffffffn, 12);
  return bytes;
}
function connection() {
  const bytes = descriptor(0x84);
  bytes[4] = 1;
  bytes[5] = 2;
  bytes.writeUInt16LE(0xabcd, 6);
  bytes.writeBigUInt64LE(0xfedcba9876543210n, 8);
  bytes.writeUInt32LE(0x76543210, 16);
  return bytes;
}
function resourceList(resources = []) {
  const header = Buffer.alloc(20);
  header.writeUInt32LE(1, 0);
  header.writeUInt32LE(15, 4);
  header.writeUInt32LE(7, 8);
  header.writeUInt16LE(1, 12);
  header.writeUInt16LE(1, 14);
  header.writeUInt32LE(resources.length, 16);
  return Buffer.concat([header, ...resources]);
}
const hex = (bytes) => [...bytes].map((byte) => byte.toString(16).padStart(2, '0')).join(',');
function registry(body, eol = '\r\n') {
  return Buffer.concat([Buffer.from([0xff, 0xfe]), Buffer.from(['Windows Registry Editor Version 5.00', '', ...body].join(eol), 'utf16le')]);
}
function rejects(code, fn) {
  assert.throws(fn, (error) => {
    assert.ok(error instanceof WindowsResourceDecodeError);
    assert.equal(error.code, code);
    assert.ok(Number.isInteger(error.offset));
    return true;
  });
}

test('observed ARM64 structure preserves ordinals, offsets, full-width fields and opaque tails', () => {
  const bytes = resourceList([memory(), interrupt(), connection()]);
  const result = decodeCmResourceListArm64(new Uint8Array(bytes));
  assert.equal(result.sha256, hash(bytes));
  assert.deepEqual(result.header, { fullResourceCount: 1, interfaceTypeRaw: 15, busNumber: 7, version: 1, revision: 1, count: 3 });
  assert.deepEqual(result.resources.map((resource) => [resource.ordinal, resource.offset, resource.kind]), [[0, 20, 'memory'], [1, 40, 'interrupt'], [2, 60, 'connection']]);
  assert.equal(result.resources[0].base, '0x123456789abcdef0');
  assert.equal(result.resources[0].length, 0x2000);
  assert.equal(result.resources[0].opaqueUnionTail, 0xabcdef01);
  assert.equal(result.resources[1].levelOrGroupWord, 0x10203);
  assert.equal(result.resources[1].vector, 711);
  assert.equal(result.resources[1].affinity, '18446744073709551615');
  assert.equal(result.resources[2].connectionId, '18364758544493064720');
  assert.equal(result.resources[2].reserved, 0xabcd);
  assert.equal(result.resources[2].opaqueUnionTail, 0x76543210);
  assert.deepEqual(decodeCmResourceListArm64(resourceList()).resources, []);
});

test('strict counts, exact extent and version reject truncation, alternate packing and trailing data', () => {
  rejects('SHORT_HEADER', () => decodeCmResourceListArm64(Buffer.alloc(19)));
  for (const count of [0, 2]) {
    const bytes = resourceList(); bytes.writeUInt32LE(count, 0);
    rejects('FULL_LIST_COUNT', () => decodeCmResourceListArm64(bytes));
  }
  for (const offset of [12, 14]) {
    const bytes = resourceList(); bytes.writeUInt16LE(2, offset);
    rejects('UNSUPPORTED_VERSION', () => decodeCmResourceListArm64(bytes));
  }
  const bytes = resourceList([memory()]);
  rejects('EXTENT_MISMATCH', () => decodeCmResourceListArm64(bytes.subarray(0, -1)));
  rejects('EXTENT_MISMATCH', () => decodeCmResourceListArm64(Buffer.concat([bytes, Buffer.alloc(1)])));
  rejects('EXTENT_MISMATCH', () => decodeCmResourceListArm64(Buffer.concat([bytes.subarray(0, 20), Buffer.alloc(16)])));
  const tooMany = resourceList(); tooMany.writeUInt32LE(0xffffffff, 16);
  rejects('RESOURCE_LIMIT', () => decodeCmResourceListArm64(tooMany));
});

test('unknown, variable-length and MSI resource interpretations fail closed', () => {
  for (const type of [0, 1, 5, 7, 0x80, 0xff]) {
    rejects('UNSUPPORTED_RESOURCE_TYPE', () => decodeCmResourceListArm64(resourceList([descriptor(type)])));
  }
  for (const flags of [2, 3, 0x20, 0x8000]) {
    rejects('UNSUPPORTED_INTERRUPT_FLAGS', () => decodeCmResourceListArm64(resourceList([interrupt(flags)])));
  }
  assert.equal(decodeCmResourceListArm64(resourceList([interrupt(0)])).resources[0].flags, 0);
});

test('typed UTF-16 REG extraction handles continuations and counts identical payloads once', () => {
  const bytes = resourceList([memory(), interrupt()]);
  const tokens = hex(bytes).split(',');
  const input = registry(['[HKEY_LOCAL_MACHINE\\SYNTHETIC\\Device]', '"Ignored"=hex(2):not-read',
    '"Literal"="text containing =hex(8):not a resource"',
    `"BootConfig"=hex(8):${tokens.slice(0, 23).join(',')},\\`,
    `  ${tokens.slice(23).join(',')}`, `"AllocConfig"=hex(8):${hex(bytes)}`, '; synthetic comment']);
  const result = decodeRegistryResources(input);
  assert.equal(result.sha256, hash(input));
  assert.deepEqual(result.stats, { values: 2, resources: 4, distinctResourcePayloads: 1 });
  assert.deepEqual(result.entries.map((entry) => entry.name), ['BootConfig', 'AllocConfig']);
  assert.equal(result.entries[0].section, 'HKEY_LOCAL_MACHINE\\SYNTHETIC\\Device');
  assert.equal(result.entries[0].resourceList.sha256, hash(bytes));
  assert.equal(decodeRegistryResources(registry(['[Synthetic]', `"X"=HEX(8):${hex(bytes)}`], '\n')).entries.length, 1);
});

test('REG encoding is UTF-16LE only, fatal for invalid surrogates, NUL, odd extent and wrong header', () => {
  const good = registry(['[Synthetic]', `"X"=hex(8):${hex(resourceList())}`]);
  rejects('REG_ENCODING', () => decodeRegistryResources(good.subarray(2)));
  rejects('REG_ENCODING', () => decodeRegistryResources(good.subarray(0, -1)));
  rejects('REG_ENCODING', () => decodeRegistryResources(Buffer.from([0xfe, 0xff, 0, 0])));
  rejects('REG_ENCODING', () => decodeRegistryResources(Buffer.from([0xff, 0xfe, 0, 0xd8])));
  rejects('REG_ENCODING', () => decodeRegistryResources(registry(['\0'])));
  rejects('REG_ENCODING', () => decodeRegistryResources(registry(['bare\rreturn'])));
  const wrongHeader = Buffer.from(good); wrongHeader.writeUInt16LE(0x58, 2);
  rejects('REG_HEADER', () => decodeRegistryResources(wrongHeader));
});

test('malformed hex, continuation and duplicate effective registry values are rejected', () => {
  for (const body of ['', '0', '000', '00,', ',00', '00,,00', '0 1', 'zz', '00,0g']) {
    rejects('REG_HEX', () => decodeRegistryResources(registry(['[Synthetic]', `"X"=hex(8):${body}`])));
  }
  rejects('REG_CONTINUATION', () => decodeRegistryResources(registry(['[Synthetic]', '"X"=hex(8):00,\\'])));
  rejects('REG_CONTINUATION', () => decodeRegistryResources(registry(['[Synthetic]', '"X"=hex(8):00,\\', '[Other]'])));
  rejects('REG_CONTINUATION', () => decodeRegistryResources(registry(['[Synthetic]', '"X"=hex(8):0\\', '  1'])));
  rejects('REG_HEX', () => decodeRegistryResources(registry(['[Synthetic]', '"X"=hex(8):00,\\', '  "NotHex"=1'])));
  rejects('REG_VALUE_SYNTAX', () => decodeRegistryResources(registry([`"X"=hex(8):${hex(resourceList())}`])));
  rejects('REG_VALUE_SYNTAX', () => decodeRegistryResources(registry(['[Synthetic]', '"Escaped\\Name"=hex(8):00'])));
  rejects('REG_SECTION', () => decodeRegistryResources(registry(['[-Synthetic]'])));
  rejects('REG_SECTION', () => decodeRegistryResources(registry(['[Unclosed'])));
  rejects('NO_RESOURCE_VALUES', () => decodeRegistryResources(registry(['[Synthetic]', '"X"="ignored"'])));
  const payload = hex(resourceList());
  rejects('DUPLICATE_VALUE', () => decodeRegistryResources(registry(['[Synthetic]', `"BootConfig"=hex(8):${payload}`, '[sYNTHETIC]', `"bootconfig"=hex(8):${payload}`])));
  assert.equal(decodeRegistryResources(registry(['[A]', `"X"=hex(8):${payload}`, '[B]', `"X"=hex(8):${payload}`])).entries.length, 2);
});

test('ignored value types cannot silently override a resource in either order', () => {
  const typed = `"X"=hex(8):${hex(resourceList([memory()]))}`;
  for (const unrelated of ['"x"="string"', '"x"=hex(2):00,00', '"x"=-']) {
    for (const assignments of [[typed, unrelated], [unrelated, typed]]) {
      rejects('DUPLICATE_VALUE', () => decodeRegistryResources(registry(['[Synthetic]', ...assignments])));
    }
  }
  rejects('DUPLICATE_VALUE', () => decodeRegistryResources(registry(['[Synthetic]', '@="default"', `""=hex(8):${hex(resourceList())}`])));
  for (const escaped of ['"\\X"="ignored"', '"X\\\\"="ignored"', '"X\\"Y"="ignored"']) {
    for (const assignments of [[typed, escaped], [escaped, typed]]) {
      rejects('REG_VALUE_SYNTAX', () => decodeRegistryResources(registry(['[Synthetic]', ...assignments])));
    }
  }
});

test('bounds include aggregate descriptors across repeated lists and overrides only lower limits', () => {
  const bytes = resourceList([memory(), memory()]);
  rejects('RESOURCE_LIMIT', () => decodeCmResourceListArm64(bytes, { maxResources: 1 }));
  rejects('BYTE_LIMIT', () => decodeCmResourceListArm64(bytes, { maxBytes: bytes.length - 1 }));
  const input = registry(['[Synthetic]', `"A"=hex(8):${hex(bytes)}`, `"B"=hex(8):${hex(bytes)}`]);
  rejects('RESOURCE_LIMIT', () => decodeRegistryResources(input, { maxResources: 3 }));
  rejects('VALUE_LIMIT', () => decodeRegistryResources(input, { maxValues: 1 }));
  rejects('LINE_LIMIT', () => decodeRegistryResources(input, { maxLines: 2 }));
  rejects('BYTE_LIMIT', () => decodeRegistryResources(input, { maxBytes: input.length - 1 }));
  for (const [name, maximum] of Object.entries(WINDOWS_RESOURCE_LIMITS)) {
    for (const value of [0, -1, 1.1, maximum + 1, NaN]) {
      assert.throws(() => decodeCmResourceListArm64(bytes, { [name]: value }), RangeError);
    }
  }
  assert.throws(() => decodeRegistryResources(input, { unknown: 1 }), TypeError);
  assert.throws(() => decodeCmResourceListArm64('not bytes'), TypeError);
});

test('CLI explicitly selects its layout, reads only, and reports machine-readable failures', async () => {
  const dir = await mkdtemp(join(tmpdir(), 'windows-resources-test-'));
  const cli = fileURLToPath(new URL('./windows-resources.mjs', import.meta.url));
  try {
    const file = join(dir, 'synthetic.reg');
    await writeFile(file, registry(['[Synthetic]', `"X"=hex(8):${hex(resourceList([connection()]))}`]));
    const result = spawnSync(process.execPath, [cli, '--format', 'registry-utf16-arm64', '--input', file], { encoding: 'utf8' });
    assert.equal(result.status, 0, result.stderr);
    assert.equal(JSON.parse(result.stdout).entries[0].resourceList.resources[0].kind, 'connection');
    const missingFormat = spawnSync(process.execPath, [cli, '--input', file], { encoding: 'utf8' });
    assert.notEqual(missingFormat.status, 0);
    assert.equal(JSON.parse(missingFormat.stderr).error, 'TypeError');
    const binary = join(dir, 'synthetic.bin');
    await writeFile(binary, resourceList([memory()]));
    const direct = spawnSync(process.execPath, [cli, '--format', 'cm-arm64', '--input', binary], { encoding: 'utf8' });
    assert.equal(direct.status, 0, direct.stderr);
    assert.equal(JSON.parse(direct.stdout).resources[0].kind, 'memory');
    await writeFile(binary, Buffer.alloc(2));
    const invalid = spawnSync(process.execPath, [cli, '--format', 'cm-arm64', '--input', binary], { encoding: 'utf8' });
    assert.notEqual(invalid.status, 0);
    assert.equal(JSON.parse(invalid.stderr).error, 'SHORT_HEADER');
    const directory = spawnSync(process.execPath, [cli, '--format', 'cm-arm64', '--input', dir], { encoding: 'utf8' });
    assert.notEqual(directory.status, 0);
    assert.equal(JSON.parse(directory.stderr).error, 'TypeError');
  } finally { await rm(dir, { recursive: true }); }
});
