// SPDX-License-Identifier: GPL-2.0-only
// Independently authored structural reader for observed Chromatix 3.0.1 files.
// No vendor parser, driver code, binary payload or register table is embedded.
import { createHash } from 'node:crypto';
import { open } from 'node:fs/promises';
import { resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

export const CHROMATIX_LIMITS = Object.freeze({
  maxBytes: 16 * 1024 * 1024,
  maxNodes: 100_000,
  maxRegisterRows: 100_000,
  maxTotalPayloadBytes: 32 * 1024 * 1024,
  maxResolvedValues: 1_000_000,
  maxResolvedBytes: 32 * 1024 * 1024,
});

export class ChromatixDecodeError extends Error {
  constructor(code, message, offset = 0) {
    super(message);
    this.name = 'ChromatixDecodeError';
    this.code = code;
    this.offset = offset;
  }
}

const hash = (bytes) => createHash('sha256').update(bytes).digest('hex');
const CRC_TABLE = Array.from({ length: 256 }, (_, value) => {
  for (let bit = 0; bit < 8; bit++) value = (value >>> 1) ^ ((value & 1) ? 0xedb88320 : 0);
  return value >>> 0;
});

export function crc32(bytes) {
  let value = 0xffffffff;
  for (const byte of bytes) value = (value >>> 8) ^ CRC_TABLE[(value ^ byte) & 0xff];
  return (value ^ 0xffffffff) >>> 0;
}

function optionsFor(options) {
  const result = { ...CHROMATIX_LIMITS, includeRegisters: false, includePayloads: false };
  for (const [key, value] of Object.entries(options)) {
    if (key === 'includeRegisters' || key === 'includePayloads') {
      if (typeof value !== 'boolean') throw new TypeError(`${key} must be boolean`);
    } else if (Object.hasOwn(CHROMATIX_LIMITS, key)) {
      if (!Number.isSafeInteger(value) || value < 1 || value > CHROMATIX_LIMITS[key]) {
        throw new RangeError(`${key} must be from 1 to ${CHROMATIX_LIMITS[key]}`);
      }
    } else {
      throw new TypeError(`Unknown decoder option: ${key}`);
    }
    result[key] = value;
  }
  return result;
}

/**
 * Structure corroborated against the front IMX681 and rear OV13858 modules:
 * fixed header; section 0 with 56-byte named descriptors; section 1 with their
 * referenced bytes; trailing CRC32. Descriptor fields whose meaning is unknown
 * remain opaque. Register rows are supported only when their named references
 * and lengths agree with the observed 40-byte layout. Front mode 1/5 ordered
 * pairs were independently compared with the GPL-2.0 reference data in
 * https://github.com/karsies-wq/sp11-imx681-linux/tree/b08f76f40b8d7b715bd4da6aef484f86142cc147/sensor-data
 * That comparison corroborates serialization, not electrical timing, hardware
 * routing or successful capture.
 */
export function decodeChromatix(input, options = {}) {
  if (!(input instanceof Uint8Array)) throw new TypeError('Input must be bytes');
  const config = optionsFor(options);
  const fail = (code, message, offset = 0) => {
    throw new ChromatixDecodeError(code, message, offset);
  };
  if (input.byteLength > config.maxBytes) fail('BYTE_LIMIT', 'Input exceeds the byte limit');
  const bytes = Buffer.from(input);
  if (bytes.length < 172) fail('SHORT_HEADER', 'Observed header requires at least 172 bytes');

  function ascii(offset, length, label) {
    const data = bytes.subarray(offset, offset + length);
    const end = data.indexOf(0);
    if (end < 0 || data.subarray(end).some((byte) => byte !== 0) ||
        data.subarray(0, end).some((byte) => byte < 0x20 || byte > 0x7e)) {
      fail('ASCII_FIELD', `${label} must be NUL-terminated ASCII with zero padding`, offset);
    }
    return data.subarray(0, end).toString('ascii');
  }

  if (ascii(0, 28, 'Signature') !== 'QTI Chromatix Header') {
    fail('BAD_MAGIC', 'Input does not have the observed Chromatix signature');
  }
  const endOffset = bytes.readUInt32LE(28);
  if (endOffset !== bytes.length - 4) {
    fail('LENGTH_MISMATCH', 'Declared end excludes exactly one trailing CRC32', 28);
  }
  const version = {
    major: bytes.readUInt16LE(32),
    minor: bytes.readUInt16LE(34),
    patch: bytes.readUInt32LE(36),
  };
  if (version.major !== 3 || version.minor !== 0 || version.patch !== 1) {
    fail('UNSUPPORTED_VERSION', 'Only the observed Chromatix 3.0.1 layout is supported', 32);
  }
  const storedCrc32 = bytes.readUInt32LE(endOffset);
  const computedCrc32 = crc32(bytes.subarray(0, endOffset));
  if (storedCrc32 !== computedCrc32) fail('CRC_MISMATCH', 'Trailing CRC32 does not match', endOffset);
  const parser = ascii(40, 48, 'Parser identification');
  const moduleName = ascii(88, 64, 'Module name');
  const tableOffset = bytes.readUInt32LE(160);
  const sectionCount = bytes.readUInt32LE(164);
  if (sectionCount !== 3 || tableOffset < 168 || tableOffset + sectionCount * 12 > endOffset) {
    fail('SECTION_TABLE', 'Expected three complete section descriptors after the header', 160);
  }
  const sections = [];
  const bySection = new Map();
  for (let index = 0; index < sectionCount; index++) {
    const descriptorOffset = tableOffset + index * 12;
    const id = bytes.readUInt32LE(descriptorOffset);
    const offset = bytes.readUInt32LE(descriptorOffset + 4);
    const length = bytes.readUInt32LE(descriptorOffset + 8);
    if (bySection.has(id) || id > 2) fail('SECTION_ID', 'Duplicate or unsupported section ID', descriptorOffset);
    if (offset < tableOffset + sectionCount * 12 || offset + length > endOffset) {
      fail('SECTION_BOUNDS', 'Section lies outside the file data region', descriptorOffset + 4);
    }
    const section = { id, descriptorOffset, offset, bytes: length };
    sections.push(section);
    bySection.set(id, section);
  }
  const orderedSections = [...sections].sort((a, b) => a.offset - b.offset);
  for (let i = 1; i < orderedSections.length; i++) {
    if (orderedSections[i].offset < orderedSections[i - 1].offset + orderedSections[i - 1].bytes) {
      fail('SECTION_OVERLAP', 'Section regions overlap', orderedSections[i].descriptorOffset);
    }
  }
  for (const section of sections) {
    section.sha256 = hash(bytes.subarray(section.offset, section.offset + section.bytes));
  }
  const descriptors = bySection.get(0);
  const payloads = bySection.get(1);
  if (descriptors.bytes % 56 !== 0) {
    fail('DESCRIPTOR_LENGTH', 'Descriptor section length is not a multiple of 56', descriptors.offset);
  }
  const descriptorCount = descriptors.bytes / 56;
  if (descriptorCount > config.maxNodes) fail('NODE_LIMIT', 'Descriptor count exceeds the limit', descriptors.offset);

  const nodes = [];
  const byId = new Map();
  let totalPayloadBytes = 0;
  for (let index = 0; index < descriptorCount; index++) {
    const descriptorOffset = descriptors.offset + index * 56;
    const id = bytes.readUInt32LE(descriptorOffset);
    if (id === 0 || byId.has(id)) fail('NODE_ID', 'Descriptor IDs must be nonzero and unique', descriptorOffset);
    const name = ascii(descriptorOffset + 4, 32, 'Descriptor name');
    const payloadRelativeOffset = bytes.readUInt32LE(descriptorOffset + 48);
    const length = bytes.readUInt32LE(descriptorOffset + 52);
    if (payloadRelativeOffset + length > payloads.bytes) {
      fail('PAYLOAD_BOUNDS', 'Named descriptor payload exceeds its section', descriptorOffset + 48);
    }
    totalPayloadBytes += length;
    if (totalPayloadBytes > config.maxTotalPayloadBytes) {
      fail('PAYLOAD_LIMIT', 'Aggregate referenced payload length exceeds the limit', descriptorOffset);
    }
    const payloadOffset = payloads.offset + payloadRelativeOffset;
    const node = {
      id, name, descriptorOffset,
      opaqueDescriptorWords: [36, 40, 44].map((offset) => bytes.readUInt32LE(descriptorOffset + offset)),
      payloadOffset, payloadBytes: length,
      sha256: hash(bytes.subarray(payloadOffset, payloadOffset + length)),
    };
    if (config.includePayloads) {
      node.payloadHex = bytes.subarray(payloadOffset, payloadOffset + length).toString('hex');
    }
    nodes.push(node);
    byId.set(id, node);
  }

  let resolvedValueCount = 0;
  let resolvedPayloadBytes = 0;
  function referencedWords(id, expectedName, count, sourceOffset, wordBytes = 4) {
    const node = byId.get(id);
    if (!node || node.name !== expectedName || count > config.maxRegisterRows || node.payloadBytes !== count * wordBytes) {
      fail('NAMED_REFERENCE', `Reference must identify ${expectedName} with count-matched ${wordBytes}-byte data`, sourceOffset);
    }
    // Charge every use, including repeated references to shared payloads. Bounds
    // on unique nodes alone do not bound resolved arrays or their digest work.
    resolvedValueCount += count;
    resolvedPayloadBytes += count * wordBytes;
    if (resolvedValueCount > config.maxResolvedValues || resolvedPayloadBytes > config.maxResolvedBytes) {
      fail('RESOLUTION_LIMIT', 'Aggregate resolved reference budget exceeded', sourceOffset);
    }
    return {
      nodeId: id, payloadOffset: node.payloadOffset, payloadBytes: node.payloadBytes,
      sha256: node.sha256,
      values: Array.from({ length: count }, (_, index) => bytes.readUIntLE(node.payloadOffset + index * wordBytes, wordBytes)),
    };
  }

  const registerSequences = [];
  const sequenceById = new Map();
  let registerRowCount = 0;
  for (const node of nodes.filter((entry) => entry.name === 'regSetting')) {
    if (node.payloadBytes % 40 !== 0) {
      fail('REGISTER_ROW_LENGTH', 'Observed regSetting rows require 40 bytes each', node.payloadOffset);
    }
    const rowCount = node.payloadBytes / 40;
    registerRowCount += rowCount;
    if (registerRowCount > config.maxRegisterRows) {
      fail('REGISTER_LIMIT', 'Register row count exceeds the limit', node.payloadOffset);
    }
    const rows = [];
    const widths = new Map();
    const operations = new Map();
    const resolvedDigest = createHash('sha256');
    for (let index = 0; index < rowCount; index++) {
      const offset = node.payloadOffset + index * 40;
      const words = Array.from({ length: 10 }, (_, item) => bytes.readUInt32LE(offset + item * 4));
      const [slaveCount, slaveReference, registerAddress, dataCount, dataReference,
        registerAddressBytes, registerDataBytes, operation, delayCount, delayReference] = words;
      // Nonempty slaveAddr descriptors in the EEPROM records contain uint16s.
      const slave = referencedWords(slaveReference, 'slaveAddr', slaveCount, offset + 4, 2);
      const data = referencedWords(dataReference, 'registerData', dataCount, offset + 16);
      const delay = referencedWords(delayReference, 'delayUs', delayCount, offset + 36);
      if (![0, 1, 2, 3, 4].includes(registerAddressBytes) || ![0, 1, 2, 3, 4].includes(registerDataBytes)) {
        fail('REGISTER_WIDTH', 'Register width code lies outside the observed 0..4 range', offset + 20);
      }
      // Zero-width records occur in TPG/auxiliary placeholders; retain the code
      // without treating it as a usable register width. Nonzero operation codes
      // remain opaque: their registerData may hold counts rather than values.
      if ((registerAddressBytes > 0 && registerAddress >= 2 ** (8 * registerAddressBytes)) ||
          (operation === 0 && registerDataBytes > 0 &&
            data.values.some((value) => value >= 2 ** (8 * registerDataBytes)))) {
        fail('REGISTER_VALUE_WIDTH', 'Register address or data exceeds its declared width', offset + 8);
      }
      const row = {
        offset, registerAddress, registerAddressBytes, registerDataBytes,
        widthSpecified: registerAddressBytes > 0 && registerDataBytes > 0,
        operationCode: operation,
        slaveReference: slave.nodeId, slaveAddresses: slave.values,
        dataReference: data.nodeId, data: data.values,
        delayReference: delay.nodeId, delayUs: delay.values,
      };
      // Every payload reference contributes, including data outside the row span.
      resolvedDigest.update(`${JSON.stringify(row)}\n`);
      if (config.includeRegisters) rows.push(row);
      const widthKey = `${registerAddressBytes}/${registerDataBytes}`;
      widths.set(widthKey, (widths.get(widthKey) ?? 0) + 1);
      operations.set(operation, (operations.get(operation) ?? 0) + 1);
    }
    const sequence = {
      nodeId: node.id, payloadOffset: node.payloadOffset, payloadBytes: node.payloadBytes,
      rowCount, rowSpanSha256: node.sha256,
      resolvedRowsSha256: resolvedDigest.digest('hex'),
      widths: [...widths].map(([bytes, rows]) => ({ addressDataBytes: bytes, rows })),
      operationCodes: [...operations].map(([code, rows]) => ({ code, rows })),
      ...(config.includeRegisters ? { rows } : {}),
    };
    registerSequences.push(sequence);
    sequenceById.set(node.id, sequence);
  }

  const registerOwners = [];
  for (const node of nodes.filter((entry) => entry.name === 'resSettings' || entry.name === 'initSetting')) {
    if (node.payloadBytes !== 8) {
      fail('REGISTER_OWNER_LENGTH', 'Observed register owner requires count and reference', node.payloadOffset);
    }
    const count = bytes.readUInt32LE(node.payloadOffset);
    const referenceId = bytes.readUInt32LE(node.payloadOffset + 4);
    const sequence = sequenceById.get(referenceId);
    if (!sequence || sequence.rowCount !== count) {
      fail('REGISTER_OWNER_REFERENCE', 'Register owner count/reference does not match its sequence', node.payloadOffset);
    }
    registerOwners.push({ nodeId: node.id, name: node.name, payloadOffset: node.payloadOffset, count, referenceId });
  }

  return {
    schema: 'sp11-chromatix-observed-3.0.1-v1',
    interpretation: 'structure and named references only; table agreement does not verify hardware behavior',
    bytes: bytes.length, sha256: hash(bytes),
    header: {
      version, parser, moduleName, endOffset,
      crc32: storedCrc32.toString(16).padStart(8, '0'), crc32Verified: true,
      opaqueWords: [bytes.readUInt32LE(152), bytes.readUInt32LE(156)],
    },
    stats: {
      descriptorCount, totalPayloadBytes, registerSequences: registerSequences.length,
      registerRowCount, resolvedValueCount, resolvedPayloadBytes,
    },
    sections, nodes, registerSequences, registerOwners,
    emitted: { registerValues: config.includeRegisters, payloadBytes: config.includePayloads },
  };
}

async function main(args) {
  const options = {};
  let input;
  for (let i = 0; i < args.length; i++) {
    if (args[i] === '--input' && !input && args[i + 1]) input = args[++i];
    else if (args[i] === '--include-registers' && !options.includeRegisters) options.includeRegisters = true;
    else if (args[i] === '--include-payloads' && !options.includePayloads) options.includePayloads = true;
    else throw new TypeError('Usage: chromatix.mjs --input FILE [--include-registers] [--include-payloads]');
  }
  if (!input) throw new TypeError('An --input file is required');
  const handle = await open(input, 'r');
  let bytes;
  try {
    const stat = await handle.stat();
    if (!stat.isFile()) throw new TypeError('Input must be a regular file');
    if (stat.size > CHROMATIX_LIMITS.maxBytes) {
      throw new ChromatixDecodeError('BYTE_LIMIT', 'Input exceeds the byte limit');
    }
    bytes = Buffer.alloc(stat.size + 1);
    let offset = 0;
    while (offset < bytes.length) {
      const { bytesRead } = await handle.read(bytes, offset, bytes.length - offset, null);
      if (bytesRead === 0) break;
      offset += bytesRead;
    }
    if (offset !== stat.size) throw new ChromatixDecodeError('FILE_CHANGED', 'Input length changed while reading');
    bytes = bytes.subarray(0, offset);
  } finally {
    await handle.close();
  }
  process.stdout.write(`${JSON.stringify(decodeChromatix(bytes, options), null, 2)}\n`);
}

if (process.argv[1] && fileURLToPath(import.meta.url) === resolve(process.argv[1])) {
  main(process.argv.slice(2)).catch((error) => {
    process.stderr.write(`${JSON.stringify({
      error: error.code ?? error.name, message: error.message,
      ...(error instanceof ChromatixDecodeError ? { offset: error.offset } : {}),
    })}\n`);
    process.exitCode = 1;
  });
}
