#!/usr/bin/env node
// SPDX-License-Identifier: GPL-2.0-only
// Independently written static metadata inventory; never executes an input.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {fileURLToPath} from 'node:url';

export function splitFields(text) {
  const fields=[];let field='',quoted=false;
  for(let i=0;i<text.length;i++) {
    const c=text[i];
    if(c==='"') {
      if(quoted&&text[i+1]==='"'){field+='"';i++;}
      else quoted=!quoted;
    } else if(c===','&&!quoted){fields.push(field.trim());field='';}
    else field+=c;
  }
  if(quoted)throw new Error('Unterminated quoted INF field');
  fields.push(field.trim());return fields;
}

function uncomment(line) {
  let quoted=false;
  for(let i=0;i<line.length;i++) {
    if(line[i]==='"') {
      if(quoted&&line[i+1]==='"')i++;
      else quoted=!quoted;
    } else if(line[i]===';'&&!quoted)return line.slice(0,i).trim();
  }
  return line.trim();
}

export function decodeInf(bytes) {
  if(bytes[0]===0xff&&bytes[1]===0xfe){if((bytes.length-2)%2)throw new Error('Odd UTF-16LE length');return {encoding:'UTF-16LE',text:bytes.subarray(2).toString('utf16le')};}
  if(bytes[0]===0xfe&&bytes[1]===0xff){const b=Buffer.from(bytes.subarray(2));if(b.length%2)throw new Error('Odd UTF-16BE length');b.swap16();return {encoding:'UTF-16BE',text:b.toString('utf16le')};}
  try{return {encoding:'UTF-8',text:new TextDecoder('utf-8',{fatal:true}).decode(bytes)};}
  catch{return {encoding:'Windows-1252',text:new TextDecoder('windows-1252').decode(bytes)};}
}

export function parseInf(bytes) {
  if(bytes.length>16*1024*1024)throw new Error('INF exceeds size bound');
  const {text,encoding}=decodeInf(bytes),sections=new Map();let section=null,continued='';
  const lines=text.split(/\r?\n/);
  if(lines.length>200000)throw new Error('INF exceeds line bound');
  for(let i=0;i<lines.length;i++) {
    let line=continued+uncomment(lines[i]);continued='';
    if(line.endsWith('\\')){continued=line.slice(0,-1);continue;}
    if(!line)continue;
    const match=/^\[([^\]]+)\]$/.exec(line);
    if(match){section=match[1].toLowerCase();if(!sections.has(section))sections.set(section,[]);continue;}
    if(section===null)continue;
    const eq=line.indexOf('=');sections.get(section).push({line:i+1,key:eq<0?null:line.slice(0,eq).trim(),value:eq<0?line:line.slice(eq+1).trim()});
  }
  if(continued)throw new Error('Unterminated INF continuation');
  const strings=new Map(),warnings=[];
  for(const name of ['strings','strings.0409'])for(const row of sections.get(name)||[])if(row.key!==null) {
    try{strings.set(row.key.toLowerCase(),splitFields(row.value)[0]);}
    catch(e){warnings.push({section:name,line:row.line,key:row.key,message:e.message});}
  }
  const expand=value=>{
    let current=value;
    for(let i=0;i<8;i++){const next=current.replace(/%([^%]+)%/g,(whole,key)=>strings.get(key.toLowerCase())??whole);if(next===current)return next;current=next;}
    return current;
  };
  const version=Object.fromEntries((sections.get('version')||[]).filter(x=>x.key!==null).map(x=>{const values=splitFields(x.value).map(expand);return [x.key.toLowerCase(),values.length===1?values[0]:values];}));
  const declared=new Set(),missing=[];
  for(const row of sections.get('manufacturer')||[]) {
    const [base,...decorations]=splitFields(expand(row.value));
    for(const name of decorations.length?decorations.map(d=>`${base}.${d}`):[base]) {
      const key=name.toLowerCase();if(sections.has(key))declared.add(key);else missing.push(name);
    }
  }
  const models=[];
  for(const name of declared)for(const row of sections.get(name)) {
    if(row.key===null)continue;
    const [installSection,...identities]=splitFields(expand(row.value));
    // INF model positions define identities; GUID and software enumerators are
    // valid too. A guessed bus-name grammar would silently lose whole drivers.
    const [hardwareID,...compatibleIDs]=identities;
    if(hardwareID)models.push({section:name,line:row.line,description:expand(row.key).replace(/^"|"$/g,''),installSection,hardwareID,compatibleIDs:compatibleIDs.filter(x=>x.length>0)});
  }
  const services=[],serviceBinaries=[],sourceFiles=[];
  for(const [name,rows] of sections)for(const row of rows) {
    if(row.key?.toLowerCase()==='addservice')services.push({section:name,line:row.line,fields:splitFields(expand(row.value))});
    if(row.key?.toLowerCase()==='servicebinary')serviceBinaries.push({section:name,line:row.line,value:expand(splitFields(row.value)[0])});
    if(/^sourcedisksfiles(?:\.|$)/.test(name)&&row.key!==null)sourceFiles.push({section:name,line:row.line,name:expand(row.key)});
  }
  return {encoding,version,models,missingDeclaredModelSections:missing,services,serviceBinaries,sourceFiles,warnings,limits:['Declared package bindings only; not evidence of installed hardware.','AddReg and executable driver semantics are not inferred.','Malformed string declarations are reported and left unresolved.']};
}

export function inspectPEHeader(bytes) {
  if(bytes.length<64||bytes.toString('ascii',0,2)!=='MZ')return null;
  const offset=bytes.readUInt32LE(0x3c);
  if(offset>bytes.length-24||bytes.toString('ascii',offset,offset+4)!=='PE\0\0')return {format:'MZ',status:'PE header unavailable or invalid'};
  const machine=bytes.readUInt16LE(offset+4),names={0xaa64:'ARM64',0x8664:'AMD64',0x14c:'I386',0xa641:'ARM64EC',0xa64e:'ARM64X'};
  return {format:'PE',machine:`0x${machine.toString(16)}`,architecture:names[machine]??'unknown',sections:bytes.readUInt16LE(offset+6)};
}

async function digest(file) {
  const hash=crypto.createHash('sha256');for await(const chunk of fs.createReadStream(file))hash.update(chunk);return hash.digest('hex');
}

export async function inventory(input) {
  const root=fs.realpathSync(input);if(!fs.statSync(root).isDirectory())throw new Error('Input must be an extracted directory');
  const files=[],skipped=[];
  async function walk(dir) {
    const entries=fs.readdirSync(dir,{withFileTypes:true}).sort((a,b)=>a.name.localeCompare(b.name,'en'));
    for(const entry of entries) {
      const absolute=path.join(dir,entry.name),relative=path.relative(root,absolute).split(path.sep).join('/');
      if(entry.isSymbolicLink()){skipped.push({path:relative,reason:'symbolic link'});continue;}
      if(entry.isDirectory()){await walk(absolute);continue;}
      if(!entry.isFile()){skipped.push({path:relative,reason:'not a regular file'});continue;}
      const stat=fs.statSync(absolute),record={path:relative,size:stat.size,sha256:await digest(absolute)};
      if(/\.inf$/i.test(entry.name)){try{record.inf=parseInf(fs.readFileSync(absolute));}catch(e){record.infError=e.message;}}
      const handle=fs.openSync(absolute,'r');try{const header=Buffer.alloc(Math.min(stat.size,65536));fs.readSync(handle,header,0,header.length,0);const pe=inspectPEHeader(header);if(pe)record.native=pe;if(header.toString('ascii',0,4)==='AeoB')record.container='AeoB';}finally{fs.closeSync(handle);}
      files.push(record);
    }
  }
  await walk(root);
  const byExtension={},byArchitecture={};for(const file of files){const ext=path.extname(file.path).toLowerCase()||'(none)';byExtension[ext]=(byExtension[ext]||0)+1;if(file.native?.architecture)byArchitecture[file.native.architecture]=(byArchitecture[file.native.architecture]||0)+1;}
  return {schemaVersion:1,inputLabel:path.basename(root),summary:{files:files.length,bytes:files.reduce((n,f)=>n+f.size,0),infFiles:files.filter(f=>f.inf).length,infErrors:files.filter(f=>f.infError).length,aeobFiles:files.filter(f=>f.container==='AeoB').length,byExtension,byArchitecture},files,skipped,interpretation:'Static package inventory. No input executed, no files installed, and no hardware support claim.'};
}

if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url)) {
  const args=process.argv.slice(2);
  if(args.length!==1){console.error('Usage: node inventory.mjs /path/to/extracted/SurfaceUpdate > inventory.json');process.exitCode=64;}
  else try{console.log(JSON.stringify(await inventory(args[0]),null,2));}catch(e){console.error(e.message);process.exitCode=1;}
}
