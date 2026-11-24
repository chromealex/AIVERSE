// Bounded session-local handoff and native capture cache. No second viewport.
export interface LibraryGraphRequest {projectPath:string;name:string;draft:boolean}
let pending:LibraryGraphRequest|null=null;
export function requestLibraryGraph(request:LibraryGraphRequest) {
  pending=request;
  window.dispatchEvent(new Event('spectrafx:library-open'));
}
export function takeLibraryGraph(projectPath:string):LibraryGraphRequest|null {
  if(pending?.projectPath!==projectPath)return null;
  const request=pending;pending=null;return request;
}
interface Thumb {signature:string;image:string}
const thumbnails=new Map<string,Thumb>();
const key=(projectPath:string,name:string,draft:boolean)=>JSON.stringify([projectPath,name,draft]);
export function rememberLibraryFrame(projectPath:string,name:string,draft:boolean,signature:string,image:string) {
  if(!projectPath||!name||!signature||!image.startsWith('data:image/jpeg;base64,')||image.length>900000)return;
  const id=key(projectPath,name,draft);
  thumbnails.delete(id);thumbnails.set(id,{signature,image});
  while(thumbnails.size>16)thumbnails.delete(thumbnails.keys().next().value!);
  window.dispatchEvent(new Event('spectrafx:library-frame'));
}
export function libraryFrame(projectPath:string,name:string,draft:boolean,signature:string):string {
  const thumb=thumbnails.get(key(projectPath,name,draft));
  return thumb?.signature===signature?thumb.image:'';
}
