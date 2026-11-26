import {useEffect, useState, type CSSProperties} from 'react';
import {invoke} from '../bridge';
import type {StudioResult} from '../types';
import {StudioButton, StudioSection} from '../ui';

const pageSize = 48;

export default function TileAtlasPicker({project, tileset, tile, select}: {
  project:string; tileset:string; tile:number; select:(tile:number)=>void;
}) {
  const [atlas,setAtlas] = useState<StudioResult['studio.tileAtlas']>();
  const [error,setError] = useState('');
  const [page,setPage] = useState(Math.floor(Math.max(0,tile)/pageSize));
  const [lastTile,setLastTile] = useState(Math.max(0,tile));
  const manual = tile >= 0;
  useEffect(() => {
    if (!manual) return;
    let active = true;
    setError('');
    void invoke('studio.tileAtlas',{project,tileset}).then(data => {
      if (active) setAtlas(data);
    }).catch(e => { if (active) setError(String(e)); });
    return () => { active=false; };
  },[project,tileset,manual]);
  const count = atlas && atlas.columns * Math.floor(atlas.height / atlas.tileHeight);
  const first = page * pageSize;
  return <StudioSection title="Tile placement">
    <label className="ptp__row"><span>Placement</span>
      <select className="au-input" value={manual?'manual':'auto'} onChange={e=>{
        if (e.target.value==='auto') { setLastTile(tile); select(-1); }
        else { setPage(Math.floor(lastTile/pageSize)); select(lastTile); }
      }}><option value="auto">Autotile</option><option value="manual">Choose atlas tile</option></select>
    </label>
    {manual && <>
      <label className="ptp__row"><span>Tile number</span><input className="au-input" type="number" min={0} max={count ? count-1 : 2147483647} value={tile}
        onChange={e=>{const n=e.target.valueAsNumber;if(Number.isInteger(n) && e.target.validity.valid){select(n);setPage(Math.floor(n/pageSize));}}}/></label>
      {error && <p role="alert">{error}</p>}
      {atlas && count && <><div className="tile-palette" aria-label="Atlas tiles" style={{'--tile-sheet':'url('+atlas.url+')'} as CSSProperties}>
        {Array.from({length:Math.max(0,Math.min(pageSize,count-first))},(_,offset)=>{
          const n=first+offset, x=(n%atlas.columns)*atlas.tileWidth, y=Math.floor(n/atlas.columns)*atlas.tileHeight;
          return <button type="button" key={n} aria-label={'Atlas tile '+n} aria-pressed={tile===n} onClick={()=>select(n)} title={'Tile '+n}>
            <span aria-hidden="true" style={{aspectRatio:atlas.tileWidth+'/'+atlas.tileHeight,backgroundImage:'var(--tile-sheet)',
              backgroundSize:(100*atlas.width/atlas.tileWidth)+'% '+(100*atlas.height/atlas.tileHeight)+'%',
              backgroundPosition:(100*x/Math.max(1,atlas.width-atlas.tileWidth))+'% '+(100*y/Math.max(1,atlas.height-atlas.tileHeight))+'%'}}/>
            {n}</button>;
        })}</div>
        {count>pageSize && <div className="au-row"><StudioButton disabled={!page} onClick={()=>setPage(page-1)}>Previous</StudioButton>
          <span>{first+1}–{Math.min(first+pageSize,count)} of {count}</span>
          <StudioButton disabled={first+pageSize>=count} onClick={()=>setPage(page+1)}>Next</StudioButton></div>}
      </>}
    </>}
  </StudioSection>;
}
