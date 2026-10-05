/* Portable, dependency-free physics mirror. The C++ implementation is primary.
 * Load artifacts/model.json and pass its weights to action(model,env).
 * State and observation order are identical to src/windrunner.hpp.
 */
export const DT = .08, GOAL = 64, RADIUS = .22, HORIZON = 400;
const clamp = (v,lo,hi) => Math.max(lo,Math.min(hi,v));
export class WindRunner {
  constructor(seed=42) { this.reset(seed); }
  reset(seed) {
    this.seed=seed>>>0; let rng=this.seed || 0x6d2b79f5;
    const uniform=()=>{rng^=rng<<13;rng^=rng>>>17;rng^=rng<<5;return (rng>>>0)/4294967296;};
    this.phases=Array.from({length:4},()=>2*Math.PI*uniform());
    this.x=0;this.y=this.center(0)+(uniform()-.5)*.6;this.vx=.8;this.vy=0;this.t=0;this.step=0;
    this.done=false;this.terminal='running';this.return=0;return this.observe();
  }
  center(x) { let p=this.phases;return .9*Math.sin(.105*x+p[0])+.45*Math.sin(.29*x+p[1])+.25*Math.sin(.53*x+p[2]); }
  slope(x) { let p=this.phases;return .0945*Math.cos(.105*x+p[0])+.1305*Math.cos(.29*x+p[1])+.1325*Math.cos(.53*x+p[2]); }
  halfWidth(x) { return 2.65+.3*Math.sin(.18*x+this.phases[3]); }
  wind() {let p=this.phases,d=this.y-this.center(this.x);return [.6*Math.sin(.24*this.x+p[1]+.35*this.t)+.13*d,.85*Math.sin(.2*this.x+p[2]+.8*this.t)+.4*Math.sin(.62*this.x+p[0])-.12*d];}
  observe() {let c=this.center(this.x),w=this.wind();return [1,(this.y-c)/this.halfWidth(this.x),this.vx/4,this.vy/3,this.slope(this.x),(this.center(this.x+6)-c)/3,w[0]/1.5,w[1]/1.5,this.x/64,(400-this.step)/400];}
  advance(input) {
    if(this.done)throw Error('step after terminal state; reset first');
    if(input.length!==2||!input.every(Number.isFinite))throw Error('invalid action');
    let a=input.map(v=>clamp(v,-1,1)),w=this.wind(),oldX=this.x;
    this.vx+=DT*(3*a[0]+w[0]-.25*this.vx);this.vy+=DT*(4*a[1]+w[1]-1.2-.32*this.vy);
    this.x+=DT*this.vx;this.y+=DT*this.vy;this.step++;this.t=DT*this.step;
    let d=(this.y-this.center(this.x))/this.halfWidth(this.x);
    let reward=1.2*(this.x-oldX)+.08-.06*d*d-.015*(a[0]*a[0]+a[1]*a[1])-.003*this.vy*this.vy;
    if(Math.abs(this.y-this.center(this.x))+RADIUS>=this.halfWidth(this.x)||this.x < -1){this.terminal='collision';reward-=30;}
    else if(this.x>=GOAL){this.terminal='goal';reward+=25;}
    else if(this.step>=HORIZON){this.terminal='timeout';reward-=5;}
    this.done=this.terminal!=='running';this.return+=reward;return this.frame(a,reward);
  }
  frame(action=[0,0],reward=0) {
    let w=this.wind();return {step:this.step,t:this.t,x:this.x,y:this.y,vx:this.vx,vy:this.vy,center:this.center(this.x),halfWidth:this.halfWidth(this.x),windX:w[0],windY:w[1],action,reward,return:this.return,done:this.done,terminal:this.terminal};
  }
}
export function action(model,env) {
  const obs=env.observe();return model.weights.map(row=>clamp(row.reduce((s,w,j)=>s+w*obs[j],0),-1,1));
}
