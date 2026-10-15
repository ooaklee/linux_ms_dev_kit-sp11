// SPDX-License-Identifier: GPL-2.0-only
import test from 'node:test';
import assert from 'node:assert/strict';
import {decodePrivacyConfiguration,decodePlatformConfiguration,decodeSensorConfiguration} from './camera-config.mjs';

function fixture(values) {
  const records=values.map(v=>{const bytes=Buffer.alloc(12);bytes.writeUInt16LE(0);bytes.writeUInt16LE(8,2);bytes.writeBigUInt64LE(BigInt(v),4);return bytes;});
  const header=Buffer.alloc(16);header.write('AeoB');header.writeUInt32LE(16+12*values.length,4);header.writeUInt32LE(1,8);header.writeUInt16LE(3,12);header.writeUInt16LE(12*values.length,14);
  return Buffer.concat([header,...records]);
}

test('privacy descriptors separate sensor membership, mechanism and resource index',()=>{
  const decoded=decodePrivacyConfiguration(fixture([0x106,0x501,0,0,0,0]));
  assert.deepEqual(decoded.descriptors[0].sensorSlots,[1,2]);
  assert.equal(decoded.descriptors[0].resourceIndex,0);
  assert.equal(decoded.descriptors[1].resourceIndex,1);
  assert.equal(decoded.descriptors[1].mechanism,'gpio');
  assert.equal(decoded.descriptors[1].executionPath,'resource-hub-write');
  assert.equal('physicalGPIO' in decoded.descriptors[1],false);
});
test('privacy polarity and component selection are distinct fields',()=>{
  const result=decodePrivacyConfiguration(fixture([(1<<13)|(5<<10)|(1<<8)|1,(4<<10)|(3<<8)|2,0,0,0,0]));
  assert.equal(result.descriptors[0].activeLow,true);assert.equal(result.descriptors[0].resourceIndex,5);
  assert.equal(result.descriptors[1].componentIndex,4);assert.equal('activeLow' in result.descriptors[1],false);
});
test('platform fields preserve both banks without inventing camera identity',()=>{
  const decoded=decodePlatformConfiguration(fixture([0x107,0x110000,0x230010,0x1000110,0,0,0,0,0,123]));
  assert.equal(decoded.flags.sensorSlotMask,7);assert.equal(decoded.flags.hasPositionOffsets,true);
  assert.equal(decoded.sensorWords[0].windowsI2CMaster,1);assert.equal(decoded.sensorWords[0].csiPhyIndex,1);
  assert.equal(decoded.sensorWords[1].windowsI2CMaster,3);assert.equal(decoded.sensorWords[1].csiPhyIndex,2);
  assert.equal(decoded.sensorWords[2].faceAuthentication,true);assert.equal(decoded.sensorWords[4].bank,1);
  assert.equal('sensorName' in decoded.sensorWords[0],false);assert.equal(decoded.uninterpretedTail[0].value,123);
});
test('unsupported tuple sizes and large fields cannot be silently truncated',()=>{
  assert.throws(()=>decodePrivacyConfiguration(fixture([1])),/six/);
  assert.throws(()=>decodePlatformConfiguration(fixture([1])),/eight/);
  assert.throws(()=>decodePrivacyConfiguration(fixture([0x100000000n,0,0,0,0,0])),/32-bit/);
});
test('platform flags describe four logical slots and retain other bits',()=>{
  const decoded=decodePlatformConfiguration(fixture([0x1f7,0,0,0,0,0,0,0,0]));
  assert.equal(decoded.flags.sensorSlotMask,7);
  assert.equal(decoded.flags.uninterpretedBits4To7,15);
});
test('GPIO selectors six and seven are logged rather than written',()=>{
  const decoded=decodePrivacyConfiguration(fixture([(6<<10)|0x101,(7<<10)|0x101,0,0,0,0]));
  assert.equal(decoded.descriptors[0].resourceIndex,6);
  assert.equal(decoded.descriptors[1].resourceIndex,7);
  assert.equal(decoded.descriptors[0].executionPath,'log-only-selector');
  assert.equal(decoded.descriptors[1].executionPath,'log-only-selector');
});
test('slot bit four is opaque and timer bits remain separate from trigger type',()=>{
  const decoded=decodePlatformConfiguration(fixture([0x10,(2<<12)|(3<<14),0,0,0,0,0,0,0]));
  assert.equal(decoded.flags.sensorSlotMask,0);
  assert.equal(decoded.flags.uninterpretedBits4To7,1);
  assert.equal(decoded.sensorWords[0].flashCciTimerIndex,2);
  assert.equal(decoded.sensorWords[0].triggerType,3);
});

function sensorFixture(values=[0x00150020,0x12345678,0],names=['sensor-label','tuning-label']) {
  const numberRecords=fixture(values).subarray(16);
  const strings=names.map(name=>{
    const payload=Buffer.from(`${name}\0`,'ascii');
    const bytes=Buffer.alloc(4+Math.max(4,payload.length));
    bytes.writeUInt16LE(1);bytes.writeUInt16LE(payload.length,2);payload.copy(bytes,4);
    return bytes;
  });
  const children=Buffer.concat([...strings,numberRecords]);
  const header=Buffer.alloc(16);header.write('AeoB');
  header.writeUInt32LE(16+children.length,4);header.writeUInt32LE(1,8);
  header.writeUInt16LE(3,12);header.writeUInt16LE(children.length,14);
  return Buffer.concat([header,children]);
}

test('sensor probe fields separate Windows address, enum codes and identity',()=>{
  const decoded=decodeSensorConfiguration(sensorFixture());
  assert.equal(decoded.bus.windowsSlaveAddress,0x20);
  assert.equal(decoded.bus.observedCciSlaveId,0x10);
  assert.equal(decoded.bus.frequencyCode,1);
  assert.equal(decoded.bus.observedFrequencySetting,400);
  assert.equal(decoded.bus.observedAddressType,2);
  assert.equal(decoded.bus.observedDataType,2);
  assert.equal(decoded.probe.register,0x5678);
  assert.equal(decoded.probe.expectedValue,0x1234);
  assert.equal('linuxAddress' in decoded.bus,false);
  assert.equal('frequencyHz' in decoded.bus,false);
  assert.equal(decoded.names.sensorModule,'sensor-label');
});

test('sensor labels stay inert and opaque fields and enum fallback remain visible',()=>{
  const decoded=decodeSensorConfiguration(sensorFixture([0xfff00023,0xffffffff,7],['../missing','a']));
  assert.equal(decoded.names.sensorModule,'../missing');
  assert.equal(decoded.bus.windowsSlaveAddress,0x23);
  assert.equal(decoded.bus.registerDataTypeCode,3);
  assert.equal(decoded.bus.observedDataType,1);
  assert.equal(decoded.bus.uninterpretedUpperBits,0x3ff);
  assert.equal(decoded.uninterpretedTail.value,7);
  const wide=decodeSensorConfiguration(sensorFixture([0x00151234,0,0]));
  assert.equal(wide.bus.windowsSlaveAddress,0x1234);
  assert.equal(wide.bus.observedInterfaceAddressByte,0x34);
  assert.equal(wide.bus.observedCciSlaveId,0x1a);
});

test('sensor interpreter rejects unverified extensions, missing labels and wide fields',()=>{
  assert.throws(()=>decodeSensorConfiguration(fixture([1,2,3])),/two strings/);
  assert.throws(()=>decodeSensorConfiguration(sensorFixture([1,2])),/two strings/);
  assert.throws(()=>decodeSensorConfiguration(sensorFixture([1,2,3,4])),/two strings/);
  assert.throws(()=>decodeSensorConfiguration(sensorFixture([0x100000000n,2,3])),/32-bit/);
  assert.throws(()=>decodeSensorConfiguration(sensorFixture([1,2,3],['one'])),/two strings/);
});
