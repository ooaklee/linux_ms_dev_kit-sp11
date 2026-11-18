// SPDX-License-Identifier: GPL-2.0-only
import test from 'node:test';
import assert from 'node:assert/strict';
import {decodeInf,parseInf,splitFields,inspectPEHeader} from './inventory.mjs';

test('quoted commas, escaped quotes and semicolons survive field parsing',()=>{
  assert.deepEqual(splitFields('a,"b,c","d""e",f'),['a','b,c','d"e','f']);
  assert.throws(()=>splitFields('"oops'),/Unterminated/);
});
test('only manufacturer-referenced model sections define device bindings',()=>{
  const inf=Buffer.from('[Version]\nClass=Camera\nProvider=%Vendor%\n[Manufacturer]\n%Vendor%=Models,NTarm64.10.0\n[Models.NTarm64.10.0]\n%Description%=Sensor,ACPI\\SONY0681,ACPI\\SONY0681&SUBSYS_MSHW0490 ;comment\n[Unreferenced]\nignored=Fake,ACPI\\FAKE0001\n[Sensor.Services]\nAddService=Camera,2,Service\n[Service]\nServiceBinary=%12%\\sensor.sys\n[Strings]\nVendor="Microsoft; vendor"\nDescription="Front, camera"\n');
  const parsed=parseInf(inf);
  assert.equal(parsed.version.provider,'Microsoft; vendor');
  assert.equal(parsed.models.length,1);
  assert.equal(parsed.models[0].description,'Front, camera');
  assert.equal(parsed.models[0].hardwareID,'ACPI\\SONY0681');
  assert.deepEqual(parsed.models[0].compatibleIDs,['ACPI\\SONY0681&SUBSYS_MSHW0490']);
  assert.equal(parsed.serviceBinaries[0].value,'%12%\\sensor.sys');
});
test('UTF16 and continued INF rows retain the complete device identity',()=>{
  const input=Buffer.concat([Buffer.from([255,254]),Buffer.from('[Manufacturer]\r\nM=Models,NTarm64\r\n[Models.NTarm64]\r\nD=Install,\\\r\n ACPI\\DEVICE01\r\n','utf16le')]);
  assert.equal(decodeInf(input).encoding,'UTF-16LE');
  assert.equal(parseInf(input).models[0].hardwareID,'ACPI\\DEVICE01');
  assert.throws(()=>decodeInf(Buffer.from([255,254,65])),/Odd UTF-16LE/);
});
test('missing decorated sections are explicit instead of assuming support',()=>{
  const parsed=parseInf(Buffer.from('[Manufacturer]\nM=Models,NTarm64\n[Models]\nD=Install,ACPI\\DEVICE01\n'));
  assert.equal(parsed.models.length,0);assert.deepEqual(parsed.missingDeclaredModelSections,['Models.NTarm64']);
});
test('DriverVer preserves both date and package version',()=>{
  assert.deepEqual(parseInf(Buffer.from('[Version]\nDriverVer=04/09/2026,1.2.3.4\n')).version.driverver,['04/09/2026','1.2.3.4']);
});
test('GUID enumerators and software identities are retained by model position',()=>{
  const parsed=parseInf(Buffer.from('[Manufacturer]\nM=Models\n[Models]\nD=Install,{4047F442-B3B0-4517-8C6F-9EEA770022B1}\\SurfacePowerStateDriver,SWC\\Component\n'));
  assert.equal(parsed.models[0].hardwareID,'{4047F442-B3B0-4517-8C6F-9EEA770022B1}\\SurfacePowerStateDriver');
  assert.deepEqual(parsed.models[0].compatibleIDs,['SWC\\Component']);
});
test('malformed unused strings retain evidence without losing other bindings',()=>{
  const parsed=parseInf(Buffer.from('[Manufacturer]\nM=Models\n[Models]\nD=Install,ACPI\\DEVICE01\n[Strings]\nbad="missing terminator\n'));
  assert.equal(parsed.models.length,1);assert.equal(parsed.warnings.length,1);
  assert.equal(parsed.warnings[0].line,6);
});
test('PE architecture comes from bounded headers, not extension or package title',()=>{
  assert.equal(inspectPEHeader(Buffer.from('not a PE')),null);
  const bytes=Buffer.alloc(128);bytes.write('MZ');bytes.writeUInt32LE(64,0x3c);bytes.write('PE\0\0',64);bytes.writeUInt16LE(0xaa64,68);bytes.writeUInt16LE(4,70);
  assert.equal(inspectPEHeader(bytes).architecture,'ARM64');
  bytes.writeUInt32LE(999999,0x3c);assert.equal(inspectPEHeader(bytes).status,'PE header unavailable or invalid');
});
