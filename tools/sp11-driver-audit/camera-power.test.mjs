// SPDX-License-Identifier: GPL-2.0-only
import test from 'node:test';
import assert from 'node:assert/strict';
import {decodeCameraPower, POWER_FIELD_EVIDENCE} from './camera-power.mjs';

function record(kind, payload) {
  const bytes = Buffer.alloc(4 + Math.max(4, payload.length));
  bytes.writeUInt16LE(kind);
  bytes.writeUInt16LE(payload.length, 2);
  payload.copy(bytes, 4);
  return bytes;
}
const integer = value => {
  const bytes = Buffer.alloc(8);
  bytes.writeBigUInt64LE(BigInt(value));
  return record(0, bytes);
};
const string = value => record(1, Buffer.from(`${value}\0`, 'ascii'));
const tuple = children => record(3, Buffer.concat(children));
const resource = (name, fields) => tuple([string(name), tuple(fields)]);
function fixture(records) {
  const payload = Buffer.concat(records);
  const header = Buffer.alloc(12);
  header.write('AeoB');
  header.writeUInt32LE(12 + payload.length, 4);
  header.writeUInt32LE(records.length, 8);
  return Buffer.concat([header, payload]);
}
const gpio = values => resource('TLMMGPIO', values.map(integer));
const vote = (name, values) => resource('PMICVREGVOTE', [string(name), ...values.map(integer)]);
const decodeOne = record => decodeCameraPower(fixture([record])).resources[0];

test('six TLMM fields retain raw low/high levels without inventing reset polarity', () => {
  const decoded = decodeCameraPower(fixture([gpio([110, 0, 0, 1, 0, 0]), gpio([110, 1, 0, 1, 0, 0])]));
  assert.deepEqual(decoded.resources.map(item => item.rawOutputLevel), [0, 1]);
  const first = decoded.resources[0];
  assert.equal(first.gpioNumber, 110);
  assert.equal(first.outputEnableCode, 1);
  assert.equal(first.functionCode, 0);
  assert.equal(first.pullCode, 0);
  assert.equal(first.equivalentDriveStrengthMilliamp, 2);
  assert.equal('activeLow' in first, false);
  assert.equal('resetGPIO' in first, false);
  assert.equal(first.fields[0].payloadOffset, first.fields[0].offset + 4);
});

test('TLMM bitfield boundaries are explicit; unsupported values stay opaque, not masked', () => {
  const valid = decodeOne(gpio([1023, 1, 15, 1, 3, 7]));
  assert.equal(valid.gpioNumber, 1023);
  assert.equal(valid.equivalentDriveStrengthMilliamp, 16);
  for (const values of [[1024, 0, 0, 1, 0, 0], [110, 2, 0, 1, 0, 0],
    [110, 0, 16, 1, 0, 0], [110, 0, 0, 2, 0, 0], [110, 0, 0, 1, 4, 0], [110, 0, 0, 1, 0, 8]]) {
    const unsupported = decodeOne(gpio(values));
    assert.equal(unsupported.interpretation, 'opaque');
    assert.deepEqual(unsupported.raw.children[1].children.map(item => Number(item.value)), values);
  }
});

test('TLMM extension fields and strings remain opaque rather than introducing inversion', () => {
  const decoded = decodeOne(resource('TLMMGPIO', [...[110, 0, 0, 1, 0, 0].map(integer),
    string('EGPIO_ENABLE'), integer(0xffffffffffffffffn)]));
  assert.equal(decoded.opaqueTail[0].value, 'EGPIO_ENABLE');
  assert.equal(decoded.opaqueTail[1].value, '18446744073709551615');
  assert.equal('activeLow' in decoded, false);
});

test('type-one PMIC votes expose only verified voltage, enable and mode fields', () => {
  const on = decodeOne(vote('PPP_RESOURCE_ID_LDO6_M', [1, 1800000, 1, 7, 0]));
  assert.equal(on.observedInternalVoteType, 0);
  assert.equal(on.voltageMicrovolts, 1800000);
  assert.equal(on.softwareEnableVote, 1);
  assert.equal(on.hardwareModeCode, 7);
  assert.equal(on.opaqueFourthVoteField.value, '0');
  assert.equal('supplyName' in on, false);
  assert.equal('actuator' in on, false);
  const off = decodeOne(vote('../not-opened', [1, 0, 0, 7, 0]));
  assert.equal(off.resourceName, '../not-opened');
  assert.equal(off.voltageMicrovolts, 0);
  assert.equal(off.softwareEnableVote, 0);
});

test('unverified PMIC vote types and non-boolean enable values are not guessed', () => {
  const other = decodeOne(resource('PMICVREGVOTE', [string('resource'), integer(12), string('unknown')]));
  assert.equal(other.interpretation, 'opaque');
  assert.equal(other.raw.children[1].children[2].value, 'unknown');
  const enable = decodeOne(vote('PPP_RESOURCE_ID_LDO16_B', [1, 2900000, 2, 7, 0]));
  assert.equal(enable.interpretation, 'opaque');
  assert.equal('voltageMicrovolts' in enable, false);
});

test('PMIC extensions remain opaque and no name is resolved to a file or board consumer', () => {
  const item = decodeOne(resource('PMICVREGVOTE', [string('PPP_RESOURCE_ID_LDO16_B'),
    ...[1, 2900000, 1, 7, 0].map(integer), string('SUPPRESSIBLE')]));
  assert.equal(item.opaqueTail[0].value, 'SUPPRESSIBLE');
  assert.equal('autofocusSupply' in item, false);
  assert.equal('client' in item, false);
});

test('resource delays are milliseconds, with optional metadata preserved verbatim', () => {
  const result = decodeOne(resource('DELAY', [integer(10), integer(123)]));
  assert.equal(result.configuredMilliseconds, 10);
  assert.equal(result.opaqueTail[0].value, '123');
  assert.equal('microseconds' in result, false);
  assert.equal(decodeOne(resource('DELAY', [integer(0)])).configuredMilliseconds, 0);
});

test('DEVICE/DSTATE wrappers retain context and configured list order without Linux conversion', () => {
  const input = fixture([tuple([string('DEVICE'), integer(133), string('\\_SB.CAMS'),
    tuple([string('COMPONENT'), integer(0)]),
    tuple([string('DSTATE'), integer(0), gpio([110, 0, 0, 1, 0, 0]),
      resource('DELAY', [integer(1)]), gpio([110, 1, 0, 1, 0, 0])]),
    tuple([string('DSTATE'), integer(3), gpio([110, 0, 0, 1, 0, 0])])])]);
  const result = decodeCameraPower(input);
  assert.equal(result.resources[0].interpretation, 'opaque');
  assert.deepEqual(result.resources.slice(1).map(item => item.mechanism), ['TLMMGPIO', 'DELAY', 'TLMMGPIO', 'TLMMGPIO']);
  const wrapper = offset => result.wrappers.find(item => item.offset === offset);
  assert.equal(wrapper(result.resources[1].contextOffsets[0]).fields[1].value, '\\_SB.CAMS');
  assert.equal(wrapper(result.resources[1].contextOffsets[1]).fields[0].value, '0');
  assert.equal(wrapper(result.resources.at(-1).contextOffsets[1]).fields[0].value, '3');
  assert.ok(result.resources[1].offset < result.resources[2].offset);
});

test('wrapper labels are emitted once, not amplified across every child resource', () => {
  const label = 'long-inert-device-label-'.repeat(1000);
  const records = Array.from({length: 50}, () => resource('DELAY', [integer(1)]));
  const input = fixture([tuple([string('DEVICE'), string(label), tuple([string('DSTATE'), integer(0), ...records])])]);
  const result = decodeCameraPower(input);
  assert.equal(result.resources.length, 50);
  assert.equal(result.wrappers.length, 2);
  assert.equal(JSON.stringify(result).split(label).length - 1, 1);
  assert.ok(result.resources.every(item => item.contextOffsets.every(offset => Number.isSafeInteger(offset))));
  assert.equal(result.wrappers[1].parentOffset, result.wrappers[0].offset);
});

test('unknown mechanisms and unlabeled tuples remain opaque including their numeric widths', () => {
  const unknown = decodeOne(resource('FUTURE_POWER', [integer(0xffffffffffffffffn)]));
  assert.equal(unknown.interpretation, 'opaque');
  assert.equal(unknown.raw.children[1].children[0].value, '18446744073709551615');
  const unlabeled = decodeOne(tuple([integer(7)]));
  assert.equal(unlabeled.interpretation, 'opaque');
});

test('known malformed layouts, wrong types and over-wide interpreted fields fail explicitly', () => {
  for (const bad of [gpio([110]), resource('TLMMGPIO', [string('110'), ...[0, 0, 1, 0, 0].map(integer)]),
    gpio([0x100000000n, 0, 0, 1, 0, 0]), vote('rail', [1, 1800000]),
    vote('rail', [1, 0x100000000n, 1, 7, 0]), resource('DELAY', []),
    resource('DELAY', [integer(0x100000000n)]),
    tuple([string('DEVICE'), integer(1), integer(2)]),
    tuple([string('DSTATE'), string('0')]),
    tuple([string('TLMMGPIO'), tuple([...[110, 0, 0, 1, 0, 0].map(integer)]), integer(9)])]) {
    assert.throws(() => decodeCameraPower(fixture([bad])), error => error.code === 'POWER_FIELDS');
  }
});

test('shared structural limits reject hostile size, count and recursion before interpretation', () => {
  assert.throws(() => decodeCameraPower(Buffer.alloc(16 * 1024 * 1024 + 1)), error => error.code === 'BYTE_LIMIT');
  const wrongCount = fixture([gpio([110, 0, 0, 1, 0, 0])]);
  wrongCount.writeUInt32LE(2, 8);
  assert.throws(() => decodeCameraPower(wrongCount), error => error.code === 'COUNT_MISMATCH');
  let deep = integer(0);
  for (let i = 0; i < 34; i++) deep = tuple([deep]);
  assert.throws(() => decodeCameraPower(fixture([deep])), error => error.code === 'DEPTH_LIMIT');
  assert.throws(() => decodeCameraPower(fixture([])), /power tuples/);
  assert.throws(() => decodeCameraPower(fixture([integer(1)])), /power tuples/);
});

test('evidence metadata pins the actual driver and instruction-based field observations', () => {
  const result = decodeCameraPower(fixture([resource('DELAY', [integer(1)])]));
  assert.equal(result.evidence.sha256, POWER_FIELD_EVIDENCE.sha256);
  assert.match(result.evidence.tlmm.physicalLevelInstructions, /^0x/);
  assert.match(result.sourceSHA256, /^[a-f0-9]{64}$/);
  assert.equal(Object.isFrozen(POWER_FIELD_EVIDENCE), true);
});
