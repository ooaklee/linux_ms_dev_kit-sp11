// SPDX-License-Identifier: GPL-2.0-only
// Original metadata interpretation from the recorded field specification.
// These are Windows resource indices, not Linux GPIOs or device-tree values.
import {decodeAeob,readAeobBytes} from './aeob.mjs';
import {fileURLToPath} from 'node:url';
import {resolve} from 'node:path';

function words(input) {
  const decoded=decodeAeob(input);
  if(decoded.records.length!==1||decoded.records[0].kind!==3)throw new Error('Expected one observed camera tuple');
  const children=decoded.records[0].children;
  if(children.some(x=>x.kind!==0||BigInt(x.value)>0xffffffffn))throw new Error('Expected 32-bit camera fields in integer records');
  return {sha256:decoded.sha256,words:children.map(x=>({value:Number(x.value),offset:x.payloadOffset}))};
}

export function decodeSensorConfiguration(input) {
  const decoded=decodeAeob(input);
  if(decoded.records.length!==1||decoded.records[0].kind!==3)throw new Error('Expected one observed sensor tuple');
  const fields=decoded.records[0].children;
  if(fields.length!==5||fields[0].kind!==1||fields[1].kind!==1||
     fields.slice(2).some(x=>x.kind!==0||BigInt(x.value)>0xffffffffn)) {
    throw new Error('Observed sensor configuration requires two strings and three 32-bit fields');
  }
  const [bus,probe,tail]=fields.slice(2).map(x=>Number(x.value));
  const frequencyCode=(bus>>>16)&3;
  const registerAddressTypeCode=(bus>>>18)&3;
  const registerDataTypeCode=(bus>>>20)&3;
  return {
    schema:'sp11-camera-scfg-observed-v1',sourceSHA256:decoded.sha256,
    names:{sensorModule:fields[0].value,tuning:fields[1].value},
    bus:{offset:fields[2].payloadOffset,packed:`0x${bus.toString(16).padStart(8,'0')}`,
      windowsSlaveAddress:bus&0xffff,observedInterfaceAddressByte:bus&0xff,
      observedCciSlaveId:(bus>>>1)&0x7f,frequencyCode,
      observedFrequencySetting:[100,400,1000,400][frequencyCode],
      registerAddressTypeCode,registerDataTypeCode,
      observedAddressType:[1,2,3,1][registerAddressTypeCode],
      observedDataType:[1,2,3,1][registerDataTypeCode],
      uninterpretedUpperBits:bus>>>22},
    probe:{offset:fields[3].payloadOffset,register:probe&0xffff,expectedValue:probe>>>16},
    uninterpretedTail:{offset:fields[4].payloadOffset,value:tail},
    limits:['Names are labels only; no referenced file is opened.',
      'The raw Windows field is not a Linux address; the observed platform HAL shifts its interface byte right one and masks seven bits.',
      'Frequency setting units and CCI bus mapping are not established by this consumer.',
      'Enum code three uses a logged Windows fallback; it does not establish hardware capability.',
      'Observed type two uses a two-byte register address and a big-endian two-byte probe value.',
      'These probe fields do not establish power sequencing, mode tables or device-tree wiring.'],
  };
}

export function decodePrivacyConfiguration(input) {
  const source=words(input);
  if(source.words.length!==6)throw new Error('Observed privacy configuration requires six descriptors');
  const descriptors=source.words.map(({value,offset},index)=>{
    const mechanismCode=(value>>>8)&3;
    const record={index,offset,packed:`0x${value.toString(16).padStart(8,'0')}`,sensorSlots:Array.from({length:8},(_,i)=>i).filter(i=>(value&(1<<i))!==0),mechanismCode};
    if(mechanismCode===1) {
      const resourceIndex=(value>>>10)&7;
      Object.assign(record,{mechanism:'gpio',resourceIndex,activeLow:Boolean(value&(1<<13)),executionPath:resourceIndex<6?'resource-hub-write':'log-only-selector'});
    }
    else if(mechanismCode===3)Object.assign(record,{mechanism:'component-pstate',componentIndex:(value>>>10)&7});
    else if(mechanismCode===2)Object.assign(record,{mechanism:'pmic',opaqueSelectors:{low:(value>>>10)&7,high:(value>>>13)&7,resource:(value>>>16)&0x3ff}});
    else record.mechanism='inactive-or-unsupported';
    return record;
  });
  return {schema:'sp11-camera-prld-observed-v1',sourceSHA256:source.sha256,descriptors,limits:['Sensor slots require independent identity mapping.','Resource-hub indices are not physical pin numbers.','Polarity describes a Windows GPIO operation; no Linux GPIO mapping is generated.','No hardware accessed or configured.']};
}

export function decodePlatformConfiguration(input) {
  const source=words(input);
  if(source.words.length<9)throw new Error('Observed platform configuration requires flags and eight sensor words');
  const flags=source.words[0].value;
  const sensorWords=source.words.slice(1,9).map(({value,offset},index)=>({
    index,bank:Math.floor(index/4),slot:index%4,offset,packed:`0x${value.toString(16).padStart(8,'0')}`,
    orientationCode:value&15,directionCode:(value>>>4)&15,
    flashPresent:Boolean(value&(1<<8)),flashIndex:(value>>>9)&7,
    flashCciTimerIndex:(value>>>12)&3,triggerType:(value>>>14)&3,
    windowsI2CMaster:(value>>>16)&15,csiPhyIndex:(value>>>20)&15,
    faceAuthentication:Boolean(value&(1<<24)),shutterType:(value>>>25)&1,
  }));
  return {schema:'sp11-camera-pcfg-observed-v1',sourceSHA256:source.sha256,flags:{packed:`0x${flags.toString(16)}`,sensorSlotMask:flags&15,uninterpretedBits4To7:(flags>>>4)&15,hasPositionOffsets:Boolean(flags&(1<<8)),hasPrivacyShutter:Boolean(flags&(1<<9))},sensorWords,uninterpretedTail:source.words.slice(9),limits:['Four logical slots share one presence mask; two alternative word banks are preserved.','Choosing a bank requires independently verified platform selection.','Orientation codes are not converted to degrees.','Windows I2C master indices are not automatically Linux CCI bus numbers.','No device-tree or runtime configuration is generated.']};
}

if(process.argv[1]&&resolve(process.argv[1])===fileURLToPath(import.meta.url)) {
  const args=process.argv.slice(2);
  try {
    const decoders={prld:decodePrivacyConfiguration,pcfg:decodePlatformConfiguration,scfg:decodeSensorConfiguration};
    if(args.length!==4||args[0]!=='--kind'||!Object.hasOwn(decoders,args[1])||args[2]!=='--input')throw new Error('Usage: node camera-config.mjs --kind prld|pcfg|scfg --input FILE');
    const input=await readAeobBytes(args[3]);
    console.log(JSON.stringify(decoders[args[1]](input),null,2));
  }catch(error){console.error(JSON.stringify({error:error.code??error.name,message:error.message}));process.exitCode=1;}
}
