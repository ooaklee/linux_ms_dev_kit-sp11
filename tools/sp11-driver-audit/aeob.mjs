// SPDX-License-Identifier: GPL-2.0-only
// Original structural reader for the observed AeoB variant. No driver execution.
import { createHash } from 'node:crypto';
import { open } from 'node:fs/promises';
import { resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

export const AEOB_LIMITS = Object.freeze({
  maxBytes: 16 * 1024 * 1024,
  maxNodes: 100_000,
  maxDepth: 32,
});

export class AeobDecodeError extends Error {
  constructor(code, message, offset = 0) {
    super(message);
    this.name = 'AeobDecodeError';
    this.code = code;
    this.offset = offset;
  }
}

const digest = (bytes) => createHash('sha256').update(bytes).digest('hex');

function limitsFor(options) {
  const limits = { ...AEOB_LIMITS };
  for (const [key, value] of Object.entries(options)) {
    if (!Object.hasOwn(AEOB_LIMITS, key)) throw new TypeError(`Unknown decoder option: ${key}`);
    if (!Number.isSafeInteger(value) || value < 1 || value > AEOB_LIMITS[key]) {
      throw new RangeError(`${key} must be an integer from 1 to ${AEOB_LIMITS[key]}`);
    }
    limits[key] = value;
  }
  return limits;
}

/**
 * Decode structure only, using observations from this Surface driver bundle.
 * Header count is checked against parsed top-level records. Unknown tags are
 * opaque and use the observed TLV length convention; incompatible variants
 * fail rather than falling back to guessed padding or alternate counts.
 * Integer values do not imply GPIOs, physical addresses, rates or policy.
 */
export function decodeAeob(input, options = {}) {
  if (!(input instanceof Uint8Array)) throw new TypeError('Input must be bytes');
  const limits = limitsFor(options);
  if (input.byteLength > limits.maxBytes) {
    throw new AeobDecodeError('BYTE_LIMIT', 'Input exceeds the decoder byte limit');
  }
  // Work on a stable bounded copy even when the caller supplied mutable bytes.
  const bytes = Buffer.from(input);
  const fail = (code, message, offset) => {
    throw new AeobDecodeError(code, message, offset);
  };
  if (bytes.length < 12) fail('SHORT_HEADER', 'AeoB header requires 12 bytes', 0);
  if (!bytes.subarray(0, 4).equals(Buffer.from('AeoB', 'ascii'))) {
    fail('BAD_MAGIC', 'Input does not have AeoB magic', 0);
  }
  const declaredBytes = bytes.readUInt32LE(4);
  const declaredTopLevelCount = bytes.readUInt32LE(8);
  if (declaredBytes !== bytes.length) {
    fail('LENGTH_MISMATCH', 'Declared file length differs from actual length', 4);
  }
  if (declaredTopLevelCount > limits.maxNodes) {
    fail('NODE_LIMIT', 'Declared top-level count exceeds the node limit', 8);
  }
  let nodeCount = 0;
  let maxDepthSeen = 0;
  const unknownKinds = new Set();

  function records(start, end, depth) {
    if (depth > limits.maxDepth) {
      fail('DEPTH_LIMIT', 'Nested records exceed the depth limit', start);
    }
    maxDepthSeen = Math.max(maxDepthSeen, depth);
    const result = [];
    let offset = start;
    while (offset < end) {
      if (end - offset < 4) {
        fail('SHORT_RECORD_HEADER', 'Incomplete TLV header in its container', offset);
      }
      if (++nodeCount > limits.maxNodes) {
        fail('NODE_LIMIT', 'Parsed records exceed the node limit', offset);
      }
      const kind = bytes.readUInt16LE(offset);
      const payloadBytes = bytes.readUInt16LE(offset + 2);
      const payloadOffset = offset + 4;
      const payloadEnd = payloadOffset + payloadBytes;
      if (payloadEnd > end) {
        fail('RECORD_OVERRUN', 'TLV payload extends past its container', offset);
      }
      const node = { offset, kind, payloadOffset, payloadBytes };
      const payload = bytes.subarray(payloadOffset, payloadEnd);
      if (kind === 0) {
        if (payloadBytes !== 8) {
          fail('INTEGER_LENGTH', 'Observed integer kind requires an 8-byte payload', offset);
        }
        node.encoding = 'uint64-le';
        node.value = payload.readBigUInt64LE().toString(10);
      } else if (kind === 1) {
        if (payloadBytes === 0 || payload[payloadBytes - 1] !== 0) {
          fail('STRING_TERMINATOR', 'Observed string kind requires a terminal NUL', offset);
        }
        for (let i = 0; i < payloadBytes - 1; i++) {
          if (payload[i] === 0 || payload[i] > 0x7f) {
            fail('STRING_ENCODING', 'String is not single-NUL-terminated ASCII', payloadOffset + i);
          }
        }
        node.encoding = 'ascii-nul';
        node.value = payload.subarray(0, -1).toString('ascii');
      } else if (kind === 3) {
        node.encoding = 'nested-tlv';
        node.children = records(payloadOffset, payloadEnd, depth + 1);
      } else {
        node.encoding = 'opaque';
        node.sha256 = digest(payload);
        unknownKinds.add(kind);
      }
      result.push(node);
      offset = payloadEnd;
    }
    return result;
  }

  const decoded = records(12, bytes.length, 0);
  if (decoded.length !== declaredTopLevelCount) {
    fail('COUNT_MISMATCH', 'Header count differs from parsed top-level record count', 8);
  }
  return {
    schema: 'sp11-aeob-observed-v1',
    interpretation: 'structure only; field meanings and other AeoB variants are unverified',
    sha256: digest(bytes),
    bytes: bytes.length,
    header: { magic: 'AeoB', declaredBytes, declaredTopLevelCount },
    stats: {
      nodeCount,
      maxDepth: maxDepthSeen,
      unknownKinds: [...unknownKinds].sort((a, b) => a - b),
    },
    records: decoded,
  };
}

export async function readAeobBytes(path) {
  const handle = await open(path, 'r');
  try {
    const stat = await handle.stat();
    if (!stat.isFile()) throw new TypeError('Input must be a regular file');
    if (stat.size > AEOB_LIMITS.maxBytes) {
      throw new AeobDecodeError('BYTE_LIMIT', 'Input exceeds the decoder byte limit');
    }
    // One extra byte detects a file that grew after stat without unbounded read.
    const bytes = Buffer.alloc(stat.size + 1);
    let offset = 0;
    while (offset < bytes.length) {
      const { bytesRead } = await handle.read(bytes, offset, bytes.length - offset, null);
      if (bytesRead === 0) break;
      offset += bytesRead;
    }
    if (offset !== stat.size) {
      throw new AeobDecodeError('FILE_CHANGED', 'Input length changed while reading');
    }
    return bytes.subarray(0, offset);
  } finally {
    await handle.close();
  }
}

async function main(args) {
  if (args.length !== 2 || args[0] !== '--input' || !args[1]) {
    throw new TypeError('Usage: node tools/sp11-driver-audit/aeob.mjs --input FILE');
  }
  const decoded = decodeAeob(await readAeobBytes(args[1]));
  process.stdout.write(`${JSON.stringify(decoded, null, 2)}\n`);
}

if (process.argv[1] && fileURLToPath(import.meta.url) === resolve(process.argv[1])) {
  main(process.argv.slice(2)).catch((error) => {
    process.stderr.write(`${JSON.stringify({
      error: error.code ?? error.name,
      message: error.message,
      ...(error instanceof AeobDecodeError ? { offset: error.offset } : {}),
    })}\n`);
    process.exitCode = 1;
  });
}
