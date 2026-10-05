import {test} from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {createRequire} from 'node:module';
import {fileURLToPath} from 'node:url';
const here=path.dirname(fileURLToPath(import.meta.url));
const sourceRoot=process.env.LAB_SOURCE_ROOT||path.resolve(here,'../browser-preview/native-lab');
const dataRoot=process.env.LAB_DATA_ROOT||path.resolve(here,'..');
let JSDOM;
try{({JSDOM}=createRequire(import.meta.url)('jsdom'))}catch{({JSDOM}=createRequire(path.resolve(here,'../../ai-engineering-studio/frontend/package.json'))('jsdom'))}
const files={
 veil:'native-games/build/veil-seed42.json',aether:'native-world/examples/island-open.json',
 windrunner:'native-rl/artifacts/trajectory.json',chronicle:'native-games/build/chronicle-seed42.json',tempo:'native-games/build/tempo-seed42.json',
 'windrunner-model':'native-rl/artifacts/model.json','windrunner-evaluation':'native-rl/artifacts/evaluation.json',research:'research/catalog.json'
};
const load=id=>{const publicPath=path.join(sourceRoot,'data',id+'.json');return JSON.parse(fs.readFileSync(fs.existsSync(publicPath)?publicPath:path.join(dataRoot,files[id]),'utf8'))};
const browserErrors=[];
async function settle(window,condition=()=>window.document.querySelector('#workspace').getAttribute('aria-busy')==='false'){
 for(let i=0;i<50;i++){await new Promise(r=>setTimeout(r,0));if(condition())return}
 throw Error('The lab did not finish loading.');
}
async function setup(project='veil'){
 const dom=new JSDOM(fs.readFileSync(path.join(sourceRoot,'index.html'),'utf8'),{url:`https://example.test/native-lab/?project=${project}`,runScripts:'outside-only',pretendToBeVisual:true});
 const w=dom.window,draws=[],downloads=[];
 const ctx=new Proxy({}, {get:(target,k)=>target[k]??((...args)=>{for(const arg of args)if(typeof arg==='number')assert.ok(Number.isFinite(arg),`Non-finite canvas coordinate in ${k}`);draws.push([k,...args])}),set:(t,k,v)=>(t[k]=v,true)});
 w.HTMLCanvasElement.prototype.getContext=()=>ctx;
 w.Blob=class{constructor(parts){this.parts=parts}};
 w.URL.createObjectURL=blob=>{downloads.push(JSON.parse(blob.parts.join('')));return 'blob:example'};
 w.URL.revokeObjectURL=()=>{};
 w.HTMLAnchorElement.prototype.click=function(){};
 w.fetch=async url=>{const key=String(url).split('/').at(-1).replace('.json','');return {ok:true,json:async()=>load(key)}};
 w.addEventListener('error',e=>browserErrors.push(e.error||e.message));
 w.eval(fs.readFileSync(path.join(sourceRoot,'windrunner-mirror.js'),'utf8').replace(/export /g,'')+'\nwindow.WindRunner=WindRunner;window.policyAction=action;');
 const js=fs.readFileSync(path.join(sourceRoot,'lab.js'),'utf8').replace(/^import[^\n]*\n/,'');
 w.eval(js);await settle(w);
 const $=s=>w.document.querySelector(s);
 const clickText=t=>{const el=[...w.document.querySelectorAll('#project-controls button')].find(b=>b.textContent===t);assert.ok(el,'Missing '+t);el.click();return el};
 const view=async mode=>{$('#view-mode').value=mode;$('#view-mode').dispatchEvent(new w.Event('change'));await settle(w)};
 const raw=()=>JSON.parse($('#raw').textContent);
 const step=n=>{for(let i=0;i<n;i++)$('#step').click()};
 return {dom,w,$,raw,step,draws,downloads,view,clickText};
}
test('All native recordings and browser previews load, step, render finite coordinates and export their actual seeds',async()=>{
 for(const project of ['veil','aether','windrunner','chronicle','tempo']){
  const app=await setup(project),{dom,$,view,raw,step,downloads}=app;
  assert.equal($('#project-title').textContent.length>0,true);
  assert.equal($('#workspace').getAttribute('aria-labelledby'),'tab-'+project);
  step(3);assert.equal($('#error').textContent,'');
  const browserState=raw();
  if(project==='veil'){assert.equal('roles' in browserState,false);assert.equal(typeof browserState.your_private_role,'string')}
  if(project==='tempo')assert.equal('enemy' in browserState,false);
  $('#export').click();assert.equal(downloads.at(-1).mode,'interactive');assert.equal(downloads.at(-1).seed,42);
  await view('native');assert.equal(Number($('#seed').value),load(project).seed);assert.equal($('#seed').disabled,true);
  assert.equal($('#project-controls').querySelectorAll('button:not([data-native-enabled])').length,[...$('#project-controls').querySelectorAll('button:not([data-native-enabled])')].filter(b=>b.disabled).length);
  $('#frame').value=String(load(project).frames.length-1);$('#frame').dispatchEvent(new dom.window.Event('input'));
  assert.deepEqual(raw(),load(project).frames.at(-1));
  $('#export').click();assert.equal(downloads.at(-1).seed,load(project).seed);
  assert.equal($('#error').textContent,'');
  assert.ok(app.draws.length>100);
  await view('interactive');assert.equal(Number($('#seed').value),42);assert.equal($('#seed').disabled,false);
  dom.window.close();
 }
 assert.equal(browserErrors.length,0);
});
test('Trained policy reaches the goal in browser physics and stops; zero control provides a contrasting terminal episode',async()=>{
 const app=await setup('windrunner');app.step(400);const trained=app.raw();
 assert.equal(trained.terminal,'goal');assert.ok(trained.x>=64);assert.ok(trained.return>100);assert.ok(trained.step<400);
 const stopped=JSON.stringify(trained);app.step(1);assert.equal(JSON.stringify(app.raw()),stopped);
 app.$('#drone-policy').value='zero';app.$('#drone-policy').dispatchEvent(new app.w.Event('change'));app.$('#reset').click();app.step(400);
 assert.equal(app.raw().terminal,'collision');assert.ok(app.raw().return<0);app.dom.window.close();
});
test('Campaign save validation rejects invalid stats, arcs, oversized history, fractional turns and injected fields without changing state',async()=>{
 const app=await setup('chronicle'),{$,clickText,w,raw}=app;
 const initial=raw();clickText('Save locally');const valid=JSON.parse(w.localStorage.getItem('hussain-chronicle-v1'));
 for(const patch of [{hp:101},{gold:-1},{turn:1.5},{npcArc:99},{objective:4},{location:5},{events:Array(201).fill('event')},{inventory:['x'.repeat(81)]},{__injected:'bad'}]){
  w.localStorage.setItem('hussain-chronicle-v1',JSON.stringify({...valid,...patch}));const before=raw();clickText('Load local save');assert.match($('#error').textContent,/Save rejected/);assert.deepEqual(raw(),before);
 }
 w.localStorage.setItem('hussain-chronicle-v1','x'.repeat(120001));clickText('Load local save');assert.match($('#error').textContent,/Save rejected/);
 w.localStorage.setItem('hussain-chronicle-v1',JSON.stringify(valid));clickText('Load local save');assert.deepEqual(raw(),initial);assert.equal($('#error').textContent,'');
 $('#rpg-action').value='rest';for(let i=0;i<10;i++)clickText('Apply action');assert.equal(raw().hp,100);
 await app.view('native');const recorded=raw();clickText('Load local save');assert.deepEqual(raw(),recorded);assert.equal($('#error').textContent,'');app.dom.window.close();
});
test('World flow conserves preview water and native evidence exposes actual conservation results',async()=>{
 const app=await setup('aether');const before=app.raw().water.reduce((a,b)=>a+b,0);app.clickText('Toggle flow valve');app.step(240);const after=app.raw().water.reduce((a,b)=>a+b,0);
 assert.ok(Math.abs(before-after)<1e-10);assert.ok(app.raw().water[1]>20);
 await app.view('native');app.$('#frame').value=String(load('aether').frames.length-1);app.$('#frame').dispatchEvent(new app.w.Event('input'));
 assert.equal(app.raw().puzzle_solved,true);assert.ok(Math.abs(app.raw().water.total_volume-14)<1e-10);assert.match(app.$('#events').textContent,/Native validation: passed/);app.dom.window.close();
});
test('RTS resource checks prevent overdrafts and exported observation omits hidden enemy state',async()=>{
 const app=await setup('tempo');app.clickText('Queue defender (55)');app.clickText('Queue defender (55)');assert.equal(app.raw().resources,15);assert.equal(app.raw().builds.length,1);assert.match(app.$('#events').textContent,/insufficient resources/);assert.equal('enemy' in app.raw(),false);
 app.step(25);assert.equal(app.raw().builds.length,0);assert.ok(app.raw().own.length>=3);assert.equal('enemy' in app.raw(),false);app.dom.window.close();
});
test('Research scope labels, illustration labels and keyboard tab navigation remain accessible',async()=>{
 const app=await setup();await settle(app.w,()=>app.$('#research-grid').children.length===5);
 const labels=[...app.w.document.querySelectorAll('.research-card .status')].map(x=>x.textContent);assert.deepEqual(labels,Array(5).fill('IN PROGRESS'));
 assert.equal(app.w.document.querySelectorAll('.concept-band figcaption').length,2);
 app.$('#tab-veil').dispatchEvent(new app.w.KeyboardEvent('keydown',{key:'ArrowRight',bubbles:true}));await settle(app.w);assert.equal(app.$('#project-title').textContent,'Aether World');assert.equal(app.$('#tab-aether').tabIndex,0);assert.equal(app.$('#tab-veil').tabIndex,-1);app.dom.window.close();
});
