// Optional Node verification of browser physics/policy against the native executable.
import fs from 'node:fs';
import {execFileSync} from 'node:child_process';
const moduleText = fs.readFileSync(new URL('./mirror.js',import.meta.url),'utf8');
const {WindRunner,action} = await import('data:text/javascript;base64,'+Buffer.from(moduleText).toString('base64'));
const model=JSON.parse(fs.readFileSync(new URL('./artifacts/model.json',import.meta.url),'utf8'));
const fields=['t','x','y','vx','vy','center','halfWidth','windX','windY','reward','return'];
let maximum=0,frames=0;
for(const seed of [0,42,2147483659,2506658466,3970628429]) {
  const native=JSON.parse(execFileSync('./build/windrunner',['--seed',String(seed),'--steps','400','--json'],{encoding:'utf8'}));
  const env=new WindRunner(seed);const mirrored=[env.frame()];
  while(!env.done)mirrored.push(env.advance(action(model,env)));
  if(mirrored.length!==native.frames.length)throw Error(`frame count mismatch at ${seed}`);
  for(let i=0;i<mirrored.length;i++) {
    const a=mirrored[i],b=native.frames[i];
    for(const field of fields) {
      const error=Math.abs(a[field]-b[field]);maximum=Math.max(maximum,error);
      if(error>1e-8)throw Error(`${seed} frame ${i} ${field}: ${error}`);
    }
    if(a.done!==b.done||a.terminal!==b.terminal||a.step!==b.step)throw Error('discrete state mismatch');
    frames++;
  }
}
console.log(`PASS: native/browser parity on ${frames} frames across 5 seeds; maximum absolute error ${maximum}`);
