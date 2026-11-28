import { useEffect, useId, useState } from 'react';
import { Play } from 'lucide-react';
import { invoke } from '../bridge';
import type { MapPlayer, StudioGraphDocumentScope } from '../types';
import { StudioAlert, StudioButton, StudioField, StudioSelect } from '../ui';

export default function GameRoleLaunch({ scope, busy, launch }: {
  scope: StudioGraphDocumentScope; busy: boolean;
  launch: (seat: number, scope: StudioGraphDocumentScope) => Promise<void>;
}) {
  const [players, setPlayers] = useState<MapPlayer[] | null>(null);
  const [seat, setSeat] = useState(0);
  const [error, setError] = useState('');
  const id = useId();
  const { projectPath, mapPath, epoch, revision } = scope;
  useEffect(() => {
    let live = true;
    void invoke('studio.mapProperties', {scope:{projectPath,mapPath,epoch,revision}}).then(result => {
      if (!live) return;
      setPlayers(result.roster);
      setSeat(result.roster.findIndex(player => !player.waves) + 1);
    }).catch(reason => { if (live) setError(String(reason)); });
    return () => { live = false; };
  }, [projectPath,mapPath,epoch,revision]);
  if (error) return <div className="studio-game__role-launch"><StudioAlert tone="danger">{error}</StudioAlert></div>;
  if (!players?.length) return <div className="studio-game__role-launch" role="status">
    {players ? 'Add a player to the map to choose a role.' : 'Loading map players…'}
  </div>;
  return <div className="studio-game__role-launch">
    <form onSubmit={event => { event.preventDefault(); if (!busy) void launch(seat, scope); }}>
      <StudioField label="Play as" htmlFor={id}>
        <StudioSelect id={id} value={seat} disabled={busy} onChange={event => setSeat(Number(event.target.value))}>
          {players.map((player, index) => !player.waves && <option key={index} value={index+1}>
            {player.name || 'Player '+(index+1)}
          </option>)}
          <option value={0}>Bots vs bots</option>
        </StudioSelect>
      </StudioField>
      <StudioButton type="submit" variant="primary" disabled={busy}><Play size={14}/>Play map</StudioButton>
    </form>
  </div>;
}
