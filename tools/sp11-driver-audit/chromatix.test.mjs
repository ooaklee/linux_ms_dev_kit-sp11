// SPDX-License-Identifier: GPL-2.0-only
import assert from 'node:assert/strict';
import { mkdtemp, writeFile, rm } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import test from 'node:test';
import { crc32, decodeChromatix, ChromatixDecodeError } from './chromatix.mjs';

// All fixtures are independently constructed, not vendor binary excerpts.
const u32 = (...values) => {
  const bytes = Buffer.alloc(values.length * 4);
  values.forEach((value, index) => bytes.writeUInt32LE(value, index * 4));
  return bytes;
};
const finish = (bytes) => {
  bytes.writeUInt32LE(crc32(bytes.subarray(0, -4)), bytes.length - 4);
  return bytes;
};

function fixture({ repeatRows = 1, dataValues = [0x56] } = {}) {
  const definitions = [
    ['resSettings', u32(repeatRows, 2)],
    ['regSetting', Buffer.concat(Array.from({ length: repeatRows }, () =>
      u32(0, 3, 0x1234, dataValues.length, 4, 2, 1, 0, 1, 5)))],
    ['slaveAddr', Buffer.alloc(0)],
    ['registerData', u32(...dataValues)],
    ['delayUs', u32(7)],
    ['uninterpreted', u32(0xffffffff)],
  ];
  const table = 168;
  const metadata = table + 36;
  const metadataBytes = definitions.length * 56;
  const data = metadata + metadataBytes;
  const dataBytes = definitions.reduce((sum, item) => sum + item[1].length, 0);
  const footer = data + dataBytes;
  const bytes = Buffer.alloc(footer + 20 + 4);
  bytes.write('QTI Chromatix Header', 0, 'ascii');
  bytes.writeUInt32LE(bytes.length - 4, 28);
  bytes.writeUInt16LE(3, 32); bytes.writeUInt32LE(1, 36);
  bytes.write('Parameter Parser V3.4.0 (synthetic)', 40, 'ascii');
  bytes.write('synthetic.example', 88, 'ascii');
  bytes.writeUInt32LE(1, 152);
  bytes.writeUInt32LE(table, 160); bytes.writeUInt32LE(3, 164);
  u32(0, metadata, metadataBytes, 1, data, dataBytes, 2, footer, 20).copy(bytes, table);
  let relative = 0;
  definitions.forEach(([name, payload], index) => {
    const offset = metadata + index * 56;
    bytes.writeUInt32LE(index + 1, offset);
    bytes.write(name, offset + 4, 'ascii');
    bytes.writeUInt32LE(0xffffffff, offset + 44);
    bytes.writeUInt32LE(relative, offset + 48);
    bytes.writeUInt32LE(payload.length, offset + 52);
    payload.copy(bytes, data + relative);
    relative += payload.length;
  });
  return { bytes: finish(bytes), metadata, data, footer };
}

function rejects(code, bytes, options) {
  assert.throws(() => decodeChromatix(bytes, options), (error) => {
    assert.ok(error instanceof ChromatixDecodeError);
    assert.equal(error.code, code);
    return true;
  });
}

test('CRC32 has its standard independent check value', () => {
  assert.equal(crc32(Buffer.from('123456789')), 0xcbf43926);
});

test('default summaries validate references without emitting registers or payloads', () => {
  const result = decodeChromatix(fixture().bytes);
  assert.equal(result.stats.descriptorCount, 6);
  assert.equal(result.stats.registerRowCount, 1);
  assert.equal(result.header.crc32Verified, true);
  assert.equal(result.nodes[0].name, 'resSettings');
  assert.equal(result.nodes[5].opaqueDescriptorWords[2], 0xffffffff);
  assert.equal('payloadHex' in result.nodes[3], false);
  assert.equal('rows' in result.registerSequences[0], false);
  assert.deepEqual(result.registerOwners.map((owner) => [owner.count, owner.referenceId]), [[1, 2]]);
});

test('explicit private-research options expose precise named values and bytes', () => {
  const result = decodeChromatix(fixture().bytes, { includeRegisters: true, includePayloads: true });
  const row = result.registerSequences[0].rows[0];
  assert.equal(row.registerAddress, 0x1234);
  assert.deepEqual(row.data, [0x56]);
  assert.deepEqual(row.slaveAddresses, []);
  assert.deepEqual(row.delayUs, [7]);
  assert.equal(row.operationCode, 0);
  assert.equal(result.nodes[3].payloadHex, '56000000');
  assert.deepEqual(result.emitted, { registerValues: true, payloadBytes: true });
});

test('header signature, lengths, version and CRC are enforced', () => {
  rejects('SHORT_HEADER', Buffer.alloc(171));
  const { bytes } = fixture();
  const magic = Buffer.from(bytes); magic[0] = 88;
  rejects('BAD_MAGIC', magic);
  const length = Buffer.from(bytes); length.writeUInt32LE(bytes.length, 28);
  rejects('LENGTH_MISMATCH', length);
  const version = Buffer.from(bytes); version.writeUInt16LE(5, 32);
  rejects('UNSUPPORTED_VERSION', version);
  const crc = Buffer.from(bytes); crc[crc.length - 1] ^= 1;
  rejects('CRC_MISMATCH', crc);
});

test('section tables reject overlap, overflow, missing IDs and non-descriptor extents', () => {
  const { bytes, metadata } = fixture();
  const overlap = Buffer.from(bytes); overlap.writeUInt32LE(metadata, 184);
  rejects('SECTION_OVERLAP', finish(overlap));
  const overflow = Buffer.from(bytes); overflow.writeUInt32LE(0xffffffff, 176);
  rejects('SECTION_BOUNDS', finish(overflow));
  const duplicate = Buffer.from(bytes); duplicate.writeUInt32LE(0, 180);
  rejects('SECTION_ID', finish(duplicate));
  const count = Buffer.from(bytes); count.writeUInt32LE(0xffffffff, 164);
  rejects('SECTION_TABLE', finish(count));
  const extent = Buffer.from(bytes); extent.writeUInt32LE(55, 176);
  rejects('DESCRIPTOR_LENGTH', finish(extent));
});

test('named descriptors enforce ASCII, unique IDs and payload bounds', () => {
  const { bytes, metadata } = fixture();
  const name = Buffer.from(bytes); name[metadata + 4] = 0xff;
  rejects('ASCII_FIELD', finish(name));
  const duplicate = Buffer.from(bytes); duplicate.writeUInt32LE(1, metadata + 56);
  rejects('NODE_ID', finish(duplicate));
  const overflow = Buffer.from(bytes); overflow.writeUInt32LE(0xffffffff, metadata + 48);
  rejects('PAYLOAD_BOUNDS', finish(overflow));
});

test('register payload references, owner counts and value widths are validated', () => {
  const { bytes, metadata, data } = fixture();
  const missing = Buffer.from(bytes); missing.writeUInt32LE(99, data + 8 + 16);
  rejects('NAMED_REFERENCE', finish(missing));
  const wrongName = Buffer.from(bytes); wrongName.writeUInt32LE(5, data + 8 + 16);
  rejects('NAMED_REFERENCE', finish(wrongName));
  const oversizedCount = Buffer.from(bytes); oversizedCount.writeUInt32LE(0xffffffff, data + 8 + 12);
  rejects('NAMED_REFERENCE', finish(oversizedCount));
  const owner = Buffer.from(bytes); owner.writeUInt32LE(2, data);
  rejects('REGISTER_OWNER_REFERENCE', finish(owner));
  const width = Buffer.from(bytes); width.writeUInt32LE(5, data + 8 + 20);
  rejects('REGISTER_WIDTH', finish(width));
  const value = Buffer.from(bytes); value.writeUInt32LE(0x100, data + 8 + 40);
  rejects('REGISTER_VALUE_WIDTH', finish(value));
  const size = Buffer.from(bytes); size.writeUInt32LE(39, metadata + 56 + 52);
  rejects('REGISTER_ROW_LENGTH', finish(size));
});

test('zero width and unknown operations remain opaque instead of executable policy', () => {
  const { bytes, data } = fixture();
  const placeholder = Buffer.from(bytes);
  placeholder.writeUInt32LE(0, data + 8 + 20);
  placeholder.writeUInt32LE(0, data + 8 + 24);
  const row = decodeChromatix(finish(placeholder), { includeRegisters: true }).registerSequences[0].rows[0];
  assert.equal(row.widthSpecified, false);
  const opaqueOperation = Buffer.from(bytes);
  opaqueOperation.writeUInt32LE(77, data + 8 + 28);
  opaqueOperation.writeUInt32LE(0xffffffff, data + 8 + 40);
  const unknown = decodeChromatix(finish(opaqueOperation), { includeRegisters: true }).registerSequences[0].rows[0];
  assert.equal(unknown.operationCode, 77);
  assert.deepEqual(unknown.data, [0xffffffff]);
});

test('resource and option bounds cannot be expanded or bypassed', () => {
  const { bytes } = fixture();
  rejects('BYTE_LIMIT', bytes, { maxBytes: 10 });
  rejects('NODE_LIMIT', bytes, { maxNodes: 5 });
  rejects('PAYLOAD_LIMIT', bytes, { maxTotalPayloadBytes: 1 });
  assert.throws(() => decodeChromatix(bytes, { includeRegisters: 'yes' }), TypeError);
  assert.throws(() => decodeChromatix(bytes, { maxNodes: 100001 }), RangeError);
  assert.throws(() => decodeChromatix(bytes, { toString: 1 }), TypeError);
});

test('reused payload references consume the aggregate resolution budget every time', () => {
  const { bytes } = fixture({ repeatRows: 3, dataValues: Array(100).fill(0x56) });
  // Only one 400-byte data node exists, but three rows resolve it repeatedly.
  const result = decodeChromatix(bytes);
  assert.equal(result.stats.resolvedValueCount, 303);
  assert.equal(result.stats.resolvedPayloadBytes, 1212);
  rejects('RESOLUTION_LIMIT', bytes, { maxResolvedValues: 150 });
  rejects('RESOLUTION_LIMIT', bytes, { maxResolvedBytes: 600 });
  rejects('RESOLUTION_LIMIT', bytes, { maxResolvedValues: 150, includeRegisters: true });
  assert.throws(() => decodeChromatix(bytes, { maxResolvedValues: 1_000_001 }), RangeError);
});

test('CLI default and opt-in output follow the same disclosure boundaries', async () => {
  const temporary = await mkdtemp(join(tmpdir(), 'sp11-chromatix-test-'));
  try {
    const input = join(temporary, 'synthetic.bin');
    await writeFile(input, fixture().bytes);
    const cli = fileURLToPath(new URL('./chromatix.mjs', import.meta.url));
    for (const flags of [[], ['--include-registers']]) {
      const run = spawnSync(process.execPath, [cli, '--input', input, ...flags], { encoding: 'utf8' });
      assert.equal(run.status, 0, run.stderr);
      const decoded = JSON.parse(run.stdout);
      assert.equal('rows' in decoded.registerSequences[0], flags.length > 0);
    }
    await writeFile(input, Buffer.alloc(4));
    const bad = spawnSync(process.execPath, [cli, '--input', input], { encoding: 'utf8' });
    assert.equal(bad.status, 1);
    assert.equal(bad.stdout, '');
    assert.equal(JSON.parse(bad.stderr).error, 'SHORT_HEADER');
  } finally {
    await rm(temporary, { recursive: true, force: true });
  }
});
