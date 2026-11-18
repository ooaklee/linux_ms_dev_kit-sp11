// SPDX-License-Identifier: GPL-2.0-only
// Research-only, original decoder of statically observed resource fields.
// This never executes a driver or generates Linux hardware configuration.
import {decodeAeob, readAeobBytes} from './aeob.mjs';
import {resolve} from 'node:path';
import {fileURLToPath} from 'node:url';

export const POWER_FIELD_EVIDENCE = Object.freeze({
  driver: 'qcpep8380.sys',
  sha256: 'e5bdeb8c0fb8823990f7520a19c00807711cf909f91f8d67db78ffb18c1b2277',
  provider: 'REA 6.5.0 / Ghidra 12.1.4',
  tlmm: Object.freeze({
    parser: '0x1404100e0',
    conversion: '0x14040a2f0',
    construction: '0x14041fe70',
    executor: '0x14011d290',
    physicalLevelInstructions: '0x1402082e4..0x14020833c',
    nativePinLayoutBlob: '8d2b8246170b4dc4cdc63f16ada344f3b9c9bec1',
    nativePinConfigBlob: 'fd0add4e71da14d6e74b0517e1015926c6c15d1b',
  }),
  pmic: Object.freeze({
    parser: '0x140412cb0',
    inputVoteTypeConversion: '0x140440af0',
    executor: '0x140126b90',
    rpmhConversion: '0x1402271c0',
    voltageEnableModeInstructions: '0x140227a04..0x140227a30',
    nativeRegulatorBlob: '73856e3df337add151ce1bb9ce9ce4b9a836384c',
  }),
  delay: Object.freeze({
    parser: '0x140414680',
    executor: '0x140128170',
    sleepConversionInstructions: '0x140033704..0x140033710',
    stallConversionInstructions: '0x140128c5c..0x140128c7c',
  }),
});

class PowerDecodeError extends Error {
  constructor(message, offset) {
    super(message);
    this.name = 'PowerDecodeError';
    this.code = 'POWER_FIELDS';
    this.offset = offset;
  }
}

function fail(message, node) {
  throw new PowerDecodeError(message, node.offset);
}

function uint32(node, name) {
  if (!node || node.kind !== 0 || BigInt(node.value) > 0xffffffffn) {
    fail(`${name} requires a 32-bit unsigned integer`, node ?? {offset: 0});
  }
  return Number(node.value);
}

function payload(tuple, minimum) {
  const fields = tuple.children;
  if (fields.length !== 2 || fields[1].kind !== 3 || fields[1].children.length < minimum) {
    fail(`${fields[0].value} requires a nested payload with at least ${minimum} fields`, tuple);
  }
  return fields[1].children;
}

function opaque(tuple, reason, context) {
  return {offset: tuple.offset, interpretation: 'opaque', reason, contextOffsets: context, raw: tuple};
}

function decodeResource(tuple, context) {
  const mechanism = tuple.children[0].value;
  if (mechanism === 'TLMMGPIO') {
    const fields = payload(tuple, 6);
    const values = fields.slice(0, 6).map((field, index) => uint32(field, `TLMMGPIO field ${index}`));
    const [gpioNumber, rawOutputLevel, functionCode, outputEnableCode, pullCode, driveCode] = values;
    // Do not silently emulate the executor's bit masks on unsupported values.
    if (gpioNumber > 1023 || rawOutputLevel > 1 || functionCode > 15 ||
        outputEnableCode > 1 || pullCode > 3 || driveCode > 7) {
      return opaque(tuple, 'TLMMGPIO values outside the interpreted native field ranges', context);
    }
    return {
      mechanism, offset: tuple.offset, contextOffsets: context, interpretation: 'observed-fields',
      gpioNumber, rawOutputLevel, functionCode, outputEnableCode, pullCode, driveCode,
      equivalentDriveStrengthMilliamp: (driveCode + 1) * 2,
      fields: fields.slice(0, 6), opaqueTail: fields.slice(6),
    };
  }
  if (mechanism === 'PMICVREGVOTE') {
    const fields = payload(tuple, 2);
    if (fields[0].kind !== 1) fail('PMICVREGVOTE resource name requires a string', fields[0]);
    const voteTypeCode = uint32(fields[1], 'PMICVREGVOTE vote type');
    if (voteTypeCode !== 1) return opaque(tuple, 'Uninterpreted PMICVREGVOTE vote type', context);
    if (fields.length < 6) fail('PMICVREGVOTE type 1 requires six payload fields', tuple);
    const [voltageMicrovolts, softwareEnableVote, hardwareModeCode] = fields.slice(2, 5)
      .map((field, index) => uint32(field, `PMICVREGVOTE field ${index + 2}`));
    uint32(fields[5], 'PMICVREGVOTE opaque fourth vote field');
    if (softwareEnableVote > 1) {
      return opaque(tuple, 'Uninterpreted non-boolean software enable vote', context);
    }
    return {
      mechanism, offset: tuple.offset, contextOffsets: context, interpretation: 'observed-fields',
      resourceName: fields[0].value, voteTypeCode, observedInternalVoteType: 0,
      voltageMicrovolts, softwareEnableVote, hardwareModeCode,
      opaqueFourthVoteField: fields[5], opaqueTail: fields.slice(6),
      fields: fields.slice(0, 5),
    };
  }
  if (mechanism === 'DELAY') {
    const fields = payload(tuple, 1);
    return {
      mechanism, offset: tuple.offset, contextOffsets: context, interpretation: 'observed-fields',
      configuredMilliseconds: uint32(fields[0], 'DELAY duration'),
      durationField: fields[0], opaqueTail: fields.slice(1),
    };
  }
  return opaque(tuple, 'Uninterpreted resource mechanism or container', context);
}

export function decodeCameraPower(input) {
  // Bounds, recursion depth, integers, TLV lengths, counts and string encoding
  // are checked by the shared structural reader before field interpretation.
  const source = decodeAeob(input);
  if (!source.records.length || source.records.some(node => node.kind !== 3)) {
    throw new PowerDecodeError('Expected one or more camera power tuples', 12);
  }
  const resources = [];
  const wrappers = [];
  function walk(tuple, context) {
    const fields = tuple.children;
    if (!fields.length || fields[0].kind !== 1) {
      resources.push(opaque(tuple, 'Tuple does not start with a string label', context));
      return;
    }
    const tag = fields[0].value;
    if (tag === 'DEVICE' || tag === 'DSTATE') {
      // Both observed DEVICE wrappers (with or without a numeric identifier)
      // retain labels only; no platform identity or runtime policy is selected.
      const prefix = tag === 'DEVICE' && fields[1]?.kind === 0 ? 3 : 2;
      if (fields.length < prefix ||
          (tag === 'DEVICE' ? fields[prefix - 1].kind !== 1 : fields[1].kind !== 0) ||
          fields.slice(prefix).some(node => node.kind !== 3)) {
        fail(`${tag} wrapper has an unsupported field layout`, tuple);
      }
      if (tag === 'DSTATE' || prefix === 3) uint32(fields[1], `${tag} ordinal`);
      wrappers.push({tag, offset: tuple.offset, parentOffset: context.at(-1) ?? null,
        fields: fields.slice(1, prefix)});
      const nextContext = [...context, tuple.offset];
      for (const child of fields.slice(prefix)) walk(child, nextContext);
      return;
    }
    resources.push(decodeResource(tuple, context));
  }
  for (const record of source.records) walk(record, []);
  return {
    schema: 'sp11-camera-power-observed-v1', sourceSHA256: source.sha256,
    evidence: POWER_FIELD_EVIDENCE, wrappers, resources,
    limits: [
      'Research only: no executable, referenced path, device or configuration is opened or applied.',
      'Tuple order is configured order, not measured timing or proof of exclusive resource ownership.',
      'Raw GPIO levels do not establish reset identity or a Linux active-low flag.',
      'Rail names are inert labels; voltage does not establish sensor-core or autofocus wiring.',
      'Enable and voltage are consumer votes; a zero vote does not prove a shared rail is electrically off.',
      'DELAY units apply only to this observed resource path, not other sensor delay fields.',
      'Unknown mechanisms, vote types and extensions remain opaque; no Linux device-tree policy is generated.',
      'Native regulator_bulk_enable is asynchronous; its array order does not reproduce a serialized resource list.',
    ],
  };
}

if (process.argv[1] && resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  try {
    const args = process.argv.slice(2);
    if (args.length !== 2 || args[0] !== '--input' || !args[1]) {
      throw new TypeError('Usage: node camera-power.mjs --input FILE');
    }
    console.log(JSON.stringify(decodeCameraPower(await readAeobBytes(args[1])), null, 2));
  } catch (error) {
    console.error(JSON.stringify({error: error.code ?? error.name, message: error.message,
      ...(error.offset === undefined ? {} : {offset: error.offset})}));
    process.exitCode = 1;
  }
}
