import type { StudioParams, StudioSpectraPreviewState } from '../types';
type Request=StudioParams['studio.spectraPreview'];
type Result=StudioSpectraPreviewState;
type Send=(request:Request)=>Promise<Result>;
type Entry={request:Request;send:Send;key:string;promise:Promise<Result>;
  resolve:(result:Result)=>void;reject:(reason:unknown)=>void};

// One bounded FIFO for this mounted panel. The host still owns the render
// thread and serializes MCP/HTML requests; this queue never touches GPU state.
export class SpectraCommandQueue {
  private waiting:Entry[]=[];
  private running:Entry|null=null;
  private readonly limit=32;
  enqueue(request:Request,send:Send):Promise<Result> {
    const status=request.action==='status';
    const key=status?JSON.stringify([request.expectedProjectPath,request.expectedGeneration]):'';
    if(status) {
      // Do not merge across a queued mutation barrier.
      const tail=this.waiting[this.waiting.length-1];
      if(tail?.key===key)return tail.promise;
      if(!tail&&this.running?.key===key)return this.running.promise;
    }
    // Stop supersedes commands not yet dispatched in this panel. It does not
    // interrupt a native operation that has already been dispatched.
    if(request.action==='stop')this.cancelWaiting('Superseded by SpectraFX Stop');
    if(this.waiting.length>=this.limit)return Promise.reject(new Error('SpectraFX command queue is full'));
    let resolve!:(result:Result)=>void,reject!:(reason:unknown)=>void;
    const promise=new Promise<Result>((ok,fail)=>{resolve=ok;reject=fail;});
    const entry:Entry={request,send,key,promise,resolve,reject};
    // Mutations and explicit cancellation take precedence over idle polling,
    // but preserve FIFO relative to each other. No burst/step is coalesced.
    if(!status&&this.waiting.length&&this.waiting[this.waiting.length-1].key) {
      const index=this.waiting.findIndex(e=>!!e.key);
      this.waiting.splice(index,0,entry);
    } else this.waiting.push(entry);
    this.drain();
    return promise;
  }
  cancelWaiting(reason='SpectraFX preview closed') {
    for(const entry of this.waiting.splice(0))entry.reject(new Error(reason));
  }
  depth():number {return this.waiting.length+(this.running?1:0);}
  private drain() {
    if(this.running)return;
    const entry=this.waiting.shift();
    if(!entry)return;
    this.running=entry;
    // Promise turn also handles synchronous sender exceptions.
    void Promise.resolve().then(()=>entry.send(entry.request)).then(
      result=>entry.resolve(result),error=>entry.reject(error)
    ).finally(()=>{this.running=null;this.drain();});
  }
}
