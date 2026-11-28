import type {GraphSource} from './shaderGraphLayout';

export interface ShaderHistory {
 graph: GraphSource|null;
 past: GraphSource[];
 future: GraphSource[];
 dragging: boolean;
}
type Action=
 | {type:'load'|'saved'; graph:GraphSource|null}
 | {type:'edit'; graph:GraphSource}
 | {type:'move'; graph:GraphSource}
 | {type:'undo'|'redo'|'drop'};
export const emptyShaderHistory:ShaderHistory={graph:null,past:[],future:[],dragging:false};

export function shaderHistory(state:ShaderHistory,action:Action):ShaderHistory {
 switch(action.type){
  case 'load':return {...emptyShaderHistory,graph:action.graph};
  case 'saved':return {...state,graph:action.graph,dragging:false};
  case 'edit':
  case 'move':{
   if(action.graph===state.graph||!state.graph)return state;
   const dragging=action.type==='move';
   const past=dragging&&state.dragging?state.past:[...state.past.slice(-63),state.graph];
   return {graph:action.graph,past,future:[],dragging};
  }
  case 'drop':return state.dragging?{...state,dragging:false}:state;
  case 'undo':{
   const graph=state.past.at(-1);
   if(!state.graph||!graph)return state;
   return {graph,past:state.past.slice(0,-1),future:[...state.future,state.graph],dragging:false};
  }
  case 'redo':{
   const graph=state.future.at(-1);
   if(!state.graph||!graph)return state;
   return {graph,past:[...state.past,state.graph],future:state.future.slice(0,-1),dragging:false};
  }
 }
}
