// SPDX-License-Identifier: GPL-2.0-only
// Original, offline reader for an explicitly selected observed ARM64 layout.
// Structure definitions, not implementation code, were consulted:
// https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/ns-wdm-_cm_resource_list
// https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/ns-wdm-_cm_partial_resource_descriptor
//
// Evidence note: four identical QCOM0C32/MSHW0495 REG exports constitute ONE
// observation. File SHA256 a46ebcfc358440fc30c196f9dc0c14b028f06a197bd6964bcd93b374b592f061;
// BootConfig == AllocConfig, 200 bytes, resource SHA256
// f340bd8f69c8e52ce6bfcf82e6912eeea896c0c4a86ba405d5c75815bca9f857.
// Ordinals 2/3 contain 0xac15000/0xac16000, both length 0x1000; these match
// Linux cci0/cci1. IRQ vectors 492/303 additionally match GIC SPI 460/271 +32.
// These are registry allocations, NOT an evaluated _CRS or direct WDF
// translated-resource capture, and do not independently prove sensor routing.
import { createHash } from 'node:crypto';
import { open } from 'node:fs/promises';
import { resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

export const WINDOWS_RESOURCE_LIMITS = Object.freeze({
  maxBytes: 1024 * 1024,
  maxResources: 4096,
  maxValues: 64,
  maxLines: 16384,
});

export class WindowsResourceDecodeError extends Error {
  constructor(code, message, offset = 0) {
    super(message);
    this.name = 'WindowsResourceDecodeError';
    this.code = code;
    this.offset = offset;
  }
}

const digest = (bytes) => createHash('sha256').update(bytes).digest('hex');
const fail = (code, message, offset = 0) => {
  throw new WindowsResourceDecodeError(code, message, offset);
};

function limitsFor(options) {
  const limits = { ...WINDOWS_RESOURCE_LIMITS };
  for (const [key, value] of Object.entries(options)) {
    if (!Object.hasOwn(limits, key)) throw new TypeError(`Unknown decoder option: ${key}`);
    if (!Number.isSafeInteger(value) || value < 1 || value > limits[key]) {
      throw new RangeError(`${key} must be an integer from 1 to ${limits[key]}`);
    }
    limits[key] = value;
  }
  return limits;
}

function boundedCopy(input, limits) {
  if (!(input instanceof Uint8Array)) throw new TypeError('Input must be bytes');
  if (input.byteLength > limits.maxBytes) fail('BYTE_LIMIT', 'Input exceeds the byte limit');
  return Buffer.from(input);
}

const layoutLimits = [
  'Caller-selected observed ARM64 layout: 20-byte header and 20-byte partial descriptors; architecture is not autodetected.',
  'Only one full resource list, version 1/revision 1, memory, line-interrupt and connection descriptors are supported.',
  'Unknown resource types, message-signaled interrupts and variable DeviceSpecificData are rejected.',
  'Registry allocation data does not establish whether an address is raw or translated; no _CRS evaluation or WDF trace is performed.',
  'Resource ordinals and connection IDs do not identify sensor routing, Linux GPIOs, I2C channels or hardware policy.',
];

/** No MMIO, driver execution, address translation, or automatic Linux mapping. */
export function decodeCmResourceListArm64(input, options = {}) {
  const limits = limitsFor(options);
  const bytes = boundedCopy(input, limits);
  if (bytes.length < 20) fail('SHORT_HEADER', 'CM_RESOURCE_LIST needs a 20-byte header');
  const fullResourceCount = bytes.readUInt32LE(0);
  const version = bytes.readUInt16LE(12);
  const revision = bytes.readUInt16LE(14);
  const count = bytes.readUInt32LE(16);
  if (fullResourceCount !== 1) fail('FULL_LIST_COUNT', 'Only one full resource list is supported');
  if (version !== 1 || revision !== 1) {
    fail('UNSUPPORTED_VERSION', 'Only observed version 1/revision 1 is supported', 12);
  }
  if (count > limits.maxResources) fail('RESOURCE_LIMIT', 'Resource count exceeds the limit', 16);
  if (bytes.length !== 20 + count * 20) {
    fail('EXTENT_MISMATCH', 'Input extent differs from the selected fixed-descriptor layout', 16);
  }
  const resources = [];
  for (let ordinal = 0; ordinal < count; ordinal++) {
    const offset = 20 + ordinal * 20;
    const type = bytes[offset];
    const flags = bytes.readUInt16LE(offset + 2);
    const resource = { ordinal, offset, type, shareDisposition: bytes[offset + 1], flags };
    if (type === 3) {
      Object.assign(resource, {
        kind: 'memory', base: `0x${bytes.readBigUInt64LE(offset + 4).toString(16)}`,
        length: bytes.readUInt32LE(offset + 12),
        opaqueUnionTail: bytes.readUInt32LE(offset + 16),
      });
    } else if (type === 2) {
      // Only the observed line-interrupt flags 0/1 are interpreted. MSI has a
      // different raw union; do not accidentally label its words as vectors.
      if (flags & ~1) fail('UNSUPPORTED_INTERRUPT_FLAGS', 'Only line-interrupt flags 0/1 are supported', offset + 2);
      Object.assign(resource, {
        kind: 'interrupt', levelOrGroupWord: bytes.readUInt32LE(offset + 4),
        vector: bytes.readUInt32LE(offset + 8),
        affinity: bytes.readBigUInt64LE(offset + 12).toString(10),
      });
    } else if (type === 0x84) {
      Object.assign(resource, {
        kind: 'connection', connectionClass: bytes[offset + 4],
        connectionType: bytes[offset + 5], reserved: bytes.readUInt16LE(offset + 6),
        connectionId: bytes.readBigUInt64LE(offset + 8).toString(10),
        opaqueUnionTail: bytes.readUInt32LE(offset + 16),
      });
    } else {
      fail('UNSUPPORTED_RESOURCE_TYPE', `Resource type ${type} is outside the supported fixed layout`, offset);
    }
    resources.push(resource);
  }
  return {
    schema: 'windows-cm-resource-list-observed-arm64-v1', sha256: digest(bytes), bytes: bytes.length,
    header: {
      fullResourceCount, interfaceTypeRaw: bytes.readUInt32LE(4), busNumber: bytes.readUInt32LE(8),
      version, revision, count,
    },
    resources, limits: [...layoutLimits],
  };
}

/** Strict typed hex(8) extraction only, not a general REG importer/evaluator. */
export function decodeRegistryResources(input, options = {}) {
  const limits = limitsFor(options);
  const bytes = boundedCopy(input, limits);
  if (bytes.length < 2 || bytes[0] !== 0xff || bytes[1] !== 0xfe || bytes.length % 2) {
    fail('REG_ENCODING', 'Expected BOM-prefixed UTF-16LE with an even byte extent');
  }
  let text;
  try { text = new TextDecoder('utf-16le', { fatal: true }).decode(bytes.subarray(2)); }
  catch { fail('REG_ENCODING', 'Invalid UTF-16LE registry text'); }
  if (text.includes('\0') || /\r(?!\n)/.test(text)) fail('REG_ENCODING', 'Unexpected NUL or bare CR in registry text');
  let lineCount = 1;
  for (const ch of text) if (ch === '\n' && ++lineCount > limits.maxLines) {
    fail('LINE_LIMIT', 'Registry line count exceeds the limit');
  }
  const lines = text.split(/\r?\n/);
  if (lines[0] !== 'Windows Registry Editor Version 5.00') fail('REG_HEADER', 'Unsupported registry text header');
  const entries = [];
  const identities = new Set();
  let section = null;
  let totalResources = 0;
  for (let line = 1; line < lines.length; line++) {
    const current = lines[line].trim();
    if (!current || current.startsWith(';')) continue;
    if (current.startsWith('[')) {
      const match = /^\[([^\]\r\n]+)\]$/.exec(current);
      if (!match || match[1].startsWith('-')) fail('REG_SECTION', 'Unsupported registry section syntax');
      section = match[1];
      continue;
    }
    if (!current.startsWith('"') && !current.startsWith('@')) continue;
    // Track every recognizable value, including ignored types, to avoid
    // presenting an earlier/later hex(8) overwritten by X="..." or hex(2).
    // This deliberately rejects escaped names rather than guessing aliases.
    const assignment = /^(?:"([^"\\\r\n]{0,256})"|(@))=(.*)$/.exec(current);
    if (!assignment || !section) fail('REG_VALUE_SYNTAX', 'Values require a section and an unescaped quoted/default name');
    const name = assignment[2] ? '' : assignment[1];
    const identity = `${section.toLowerCase()}\0${name.toLowerCase()}`;
    if (identities.has(identity)) fail('DUPLICATE_VALUE', 'Repeated resource value has ambiguous registry semantics');
    identities.add(identity);
    // Unrelated value contents are not decoded or re-emitted. Text mentioning
    // hex(8) inside a quoted string is not a typed resource assignment.
    const valueText = assignment[3];
    if (!/^hex\(8\)/i.test(valueText)) continue;
    const match = /^hex\(8\):(.*)$/i.exec(valueText);
    if (!match) fail('REG_VALUE_SYNTAX', 'Typed hex(8) resource value requires a colon');
    if (entries.length >= limits.maxValues) fail('VALUE_LIMIT', 'Resource value count exceeds the limit');
    const parts = [];
    let fragment = match[1].trim();
    while (fragment.endsWith('\\')) {
      if (!fragment.endsWith(',\\')) fail('REG_CONTINUATION', 'Hex continuation must follow a comma');
      parts.push(fragment.slice(0, -1));
      if (++line >= lines.length) fail('REG_CONTINUATION', 'Unfinished resource value continuation');
      if (!/^[ \t]+/.test(lines[line])) fail('REG_CONTINUATION', 'Continuation must be an indented hex line');
      fragment = lines[line].trim();
    }
    parts.push(fragment);
    const body = parts.join('');
    if (!/^[ \t]*[0-9a-f]{2}(?:[ \t]*,[ \t]*[0-9a-f]{2})*[ \t]*$/i.test(body)) {
      fail('REG_HEX', 'Resource value must contain comma-separated two-digit hex bytes');
    }
    const tokens = body.split(',');
    // Check extent before allocating the decoded value or constructing nodes.
    if (tokens.length > 20 + limits.maxResources * 20) fail('RESOURCE_LIMIT', 'Resource value extent exceeds the limit');
    const payload = Buffer.from(tokens.map((token) => Number.parseInt(token.trim(), 16)));
    if (payload.length >= 20 && totalResources + payload.readUInt32LE(16) > limits.maxResources) {
      fail('RESOURCE_LIMIT', 'Aggregate registry resource count exceeds the limit', 16);
    }
    const resourceList = decodeCmResourceListArm64(payload, limits);
    totalResources += resourceList.header.count;
    entries.push({ section, name, resourceList });
  }
  if (!entries.length) fail('NO_RESOURCE_VALUES', 'No typed hex(8) resource values found');
  return {
    schema: 'windows-registry-resources-utf16-observed-arm64-v1',
    sha256: digest(bytes), bytes: bytes.length,
    stats: { values: entries.length, resources: totalResources, distinctResourcePayloads: new Set(entries.map((entry) => entry.resourceList.sha256)).size },
    entries,
    limits: [...layoutLimits, 'Only typed hex(8) contents are extracted; unrelated value contents are ignored, and nothing is imported.',
      'All repeated value identities and escaped/ambiguous value names are conservatively rejected, including unrelated value types.',
      'Identical payloads/copies are not independent hardware observations.'],
  };
}

export async function readWindowsResourceBytes(path) {
  const handle = await open(path, 'r');
  try {
    const stat = await handle.stat();
    if (!stat.isFile()) throw new TypeError('Input must be a regular file');
    if (stat.size > WINDOWS_RESOURCE_LIMITS.maxBytes) fail('BYTE_LIMIT', 'Input exceeds the byte limit');
    const bytes = Buffer.alloc(stat.size + 1);
    let offset = 0;
    while (offset < bytes.length) {
      const { bytesRead } = await handle.read(bytes, offset, bytes.length - offset, null);
      if (!bytesRead) break;
      offset += bytesRead;
    }
    if (offset !== stat.size) fail('FILE_CHANGED', 'Input length changed while reading');
    return bytes.subarray(0, offset);
  } finally { await handle.close(); }
}

async function main(args) {
  const decoders = { 'registry-utf16-arm64': decodeRegistryResources, 'cm-arm64': decodeCmResourceListArm64 };
  if (args.length !== 4 || args[0] !== '--format' || !Object.hasOwn(decoders, args[1]) || args[2] !== '--input' || !args[3]) {
    throw new TypeError('Usage: node windows-resources.mjs --format registry-utf16-arm64|cm-arm64 --input FILE');
  }
  process.stdout.write(`${JSON.stringify(decoders[args[1]](await readWindowsResourceBytes(args[3])), null, 2)}\n`);
}

if (process.argv[1] && fileURLToPath(import.meta.url) === resolve(process.argv[1])) {
  main(process.argv.slice(2)).catch((error) => {
    process.stderr.write(`${JSON.stringify({ error: error.code ?? error.name, message: error.message,
      ...(error instanceof WindowsResourceDecodeError ? { offset: error.offset } : {}) })}\n`);
    process.exitCode = 1;
  });
}
