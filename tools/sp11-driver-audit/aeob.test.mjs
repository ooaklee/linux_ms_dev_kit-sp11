// SPDX-License-Identifier: GPL-2.0-only
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { mkdtemp, rm, writeFile } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { spawnSync } from 'node:child_process';
import test from 'node:test';
import { fileURLToPath } from 'node:url';
import { AEOB_LIMITS, AeobDecodeError, decodeAeob } from './aeob.mjs';

// Synthetic fixtures are independently generated, not copied driver payloads.
function tlv(kind, payload, paddingValue = 0) {
  const header = Buffer.alloc(4);
  header.writeUInt16LE(kind, 0);
  header.writeUInt16LE(payload.length, 2);
  return Buffer.concat([header, payload, Buffer.alloc(Math.max(0, 4 - payload.length), paddingValue)]);
}

function integer(value) {
  const payload = Buffer.alloc(8);
  payload.writeBigUInt64LE(value);
  return tlv(0, payload);
}

function container(children) { return tlv(3, Buffer.concat(children)); }

function file(records, count = records.length) {
  const body = Buffer.concat(records);
  const header = Buffer.alloc(12);
  header.write('AeoB', 0, 'ascii');
  header.writeUInt32LE(body.length + 12, 4);
  header.writeUInt32LE(count, 8);
  return Buffer.concat([header, body]);
}

function rejects(code, bytes, options) {
  assert.throws(() => decodeAeob(bytes, options), (error) => {
    assert.ok(error instanceof AeobDecodeError);
    assert.equal(error.code, code);
    assert.ok(Number.isInteger(error.offset));
    return true;
  });
}

test('nested structure preserves precise unsigned 64-bit values and offsets', () => {
  const bytes = file([container([
    integer(18_446_744_073_709_551_615n),
    tlv(1, Buffer.from('reference\0', 'ascii')),
  ])]);
  const result = decodeAeob(bytes);
  assert.equal(result.header.declaredTopLevelCount, 1);
  assert.deepEqual(result.stats, { nodeCount: 3, maxDepth: 1, unknownKinds: [] });
  assert.equal(result.records[0].offset, 12);
  assert.equal(result.records[0].children[0].payloadOffset, 20);
  assert.equal(result.records[0].children[0].value, '18446744073709551615');
  assert.equal(result.records[0].children[1].value, 'reference');
  assert.equal(result.sha256, createHash('sha256').update(bytes).digest('hex'));
});

test('empty structure and empty terminated string are valid', () => {
  assert.deepEqual(decodeAeob(file([])).records, []);
  assert.equal(decodeAeob(file([tlv(1, Buffer.from([0]))])).records[0].value, '');
  assert.deepEqual(decodeAeob(file([container([])])).records[0].children, []);
});

test('unknown kind remains opaque with length, offsets and digest only', () => {
  const payload = Buffer.from([0xff, 0, 7, 4]);
  const result = decodeAeob(file([tlv(17, payload)]));
  assert.deepEqual(result.stats.unknownKinds, [17]);
  assert.deepEqual(result.records[0], {
    offset: 12, kind: 17, payloadOffset: 16, payloadBytes: 4,
    encoding: 'opaque', sha256: createHash('sha256').update(payload).digest('hex'),
  });
  assert.equal('value' in result.records[0], false);
});

test('short arguments consume a minimum four-byte data union, not alignment', () => {
  const payload = Buffer.from([0x11, 0x22]);
  const result = decodeAeob(file([container([
    tlv(2, payload, 0xa5),
    tlv(1, Buffer.from('a\0')),
    tlv(1, Buffer.from('four\0')),
    integer(9n),
  ])]));
  const children = result.records[0].children;
  assert.equal(children[0].encoding, 'buffer');
  assert.equal(children[0].paddingBytes, 2);
  assert.equal(children[0].sha256, createHash('sha256').update(payload).digest('hex'));
  assert.deepEqual(result.stats.unknownKinds, []);
  assert.deepEqual(children.map((item) => item.offset), [16, 24, 32, 41]);
  assert.equal(children[3].value, '9');
  // Padding belongs to the data union, but may not run into a sibling.
  const truncated = Buffer.from([2, 0, 2, 0, 0x11, 0x22]);
  rejects('RECORD_OVERRUN', file([truncated]));
  rejects('RECORD_OVERRUN', file([container([truncated]), integer(1n)]));
});

test('documented ULONG and observed uint64 integer forms preserve precision', () => {
  const payload = Buffer.alloc(4);
  payload.writeUInt32LE(0xffff_ffff);
  const result = decodeAeob(file([tlv(0, payload)]));
  assert.equal(result.records[0].encoding, 'uint32-le');
  assert.equal(result.records[0].value, '4294967295');
});

test('header, byte limits and declared counts are enforced', () => {
  rejects('SHORT_HEADER', Buffer.alloc(11));
  const magic = file([]); magic[0] = 0;
  rejects('BAD_MAGIC', magic);
  const length = file([]); length.writeUInt32LE(13, 4);
  rejects('LENGTH_MISMATCH', length);
  rejects('COUNT_MISMATCH', file([integer(1n)], 0));
  rejects('COUNT_MISMATCH', file([container([integer(1n)])], 2));
  rejects('NODE_LIMIT', file([], AEOB_LIMITS.maxNodes + 1));
  rejects('BYTE_LIMIT', file([integer(1n)]), { maxBytes: 20 });
});

test('container length cannot consume siblings or partial headers', () => {
  rejects('SHORT_RECORD_HEADER', file([Buffer.from([0, 0, 8])]));
  const overrun = Buffer.from([0, 0, 8, 0, 1]);
  rejects('RECORD_OVERRUN', file([overrun]));
  rejects('SHORT_RECORD_HEADER', file([container([Buffer.from([0, 0])])]));
  rejects('RECORD_OVERRUN', file([container([overrun]), integer(1n)]));
  // Bytes beyond the minimum data union are not silently treated as padding.
  rejects('SHORT_RECORD_HEADER', file([container([
    tlv(2, Buffer.from([0x11, 0x22])), Buffer.from([0, 0]),
  ])]));
});

test('observed integer and string encodings reject unsupported forms', () => {
  rejects('INTEGER_LENGTH', file([tlv(0, Buffer.alloc(7))]));
  rejects('STRING_TERMINATOR', file([tlv(1, Buffer.alloc(0))]));
  rejects('STRING_TERMINATOR', file([tlv(1, Buffer.from('no nul'))]));
  rejects('STRING_ENCODING', file([tlv(1, Buffer.from([65, 0, 66, 0]))]));
  rejects('STRING_ENCODING', file([tlv(1, Buffer.from([0xc3, 0xa9, 0]))]));
});

test('recursive depth and total record counts are independently bounded', () => {
  const nested = file([container([container([integer(1n)])])]);
  rejects('DEPTH_LIMIT', nested, { maxDepth: 1 });
  rejects('NODE_LIMIT', nested, { maxNodes: 2 });
  assert.equal(decodeAeob(nested, { maxDepth: 2, maxNodes: 3 }).stats.nodeCount, 3);
  for (const options of [{ maxBytes: 0 }, { maxNodes: 1.2 }, { maxDepth: 33 }]) {
    assert.throws(() => decodeAeob(file([]), options), RangeError);
  }
  assert.throws(() => decodeAeob(file([]), { guessedPadding: true }), TypeError);
  assert.throws(() => decodeAeob(file([]), { toString: 1 }), TypeError);
  assert.throws(() => decodeAeob('AeoB'), TypeError);
});

test('CLI emits JSON on success and a bounded diagnostic on failure', async () => {
  const temporary = await mkdtemp(join(tmpdir(), 'sp11-aeob-test-'));
  try {
    const input = join(temporary, 'synthetic.bin');
    await writeFile(input, file([integer(42n)]));
    const cli = fileURLToPath(new URL('./aeob.mjs', import.meta.url));
    const ok = spawnSync(process.execPath, [cli, '--input', input], { encoding: 'utf8' });
    assert.equal(ok.status, 0, ok.stderr);
    assert.equal(JSON.parse(ok.stdout).records[0].value, '42');
    assert.equal(ok.stderr, '');
    await writeFile(input, Buffer.alloc(4));
    const bad = spawnSync(process.execPath, [cli, '--input', input], { encoding: 'utf8' });
    assert.equal(bad.status, 1);
    assert.equal(bad.stdout, '');
    assert.equal(JSON.parse(bad.stderr).error, 'SHORT_HEADER');
  } finally {
    await rm(temporary, { recursive: true, force: true });
  }
});
