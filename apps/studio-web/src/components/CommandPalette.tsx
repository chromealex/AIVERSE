import { useEffect, useMemo, useRef, useState } from 'react';
import { Search } from 'lucide-react';
import { searchCommands, type CommandEntry, type CommandKind } from '../commandSearch';
import './CommandPalette.css';

const RECENT_KEY = 'aiverse.studio.palette.recent';
function readRecent(): string[] {
  try { const v = JSON.parse(window.localStorage.getItem(RECENT_KEY) ?? '[]'); return Array.isArray(v) ? v.filter(x => typeof x === 'string').slice(0, 20) : []; }
  catch { return []; }
}
const FILTERS: Array<{ prefix: string; label: string }> = [
  { prefix: '', label: 'All' }, { prefix: '>', label: '> commands' }, { prefix: '@', label: '@ scene' }, { prefix: '#', label: '# assets' },
];
const KIND_COLORS: Record<CommandKind, string> = {
  command: 'var(--au-accent-strong)', workspace: '#29496c', window: '#3b4f6b', entity: '#5a6b82', asset: '#4f7d5f',
};

/** ⌘K palette: commands, workspaces, windows (legacy flagged), scene entities and assets. */
export default function CommandPalette({ entries, onClose, initial = '' }: {
  entries: CommandEntry[]; onClose: () => void; initial?: string;
}) {
  const [query, setQuery] = useState(initial);
  const [recent, setRecent] = useState(readRecent);
  const [active, setActive] = useState(0);
  const input = useRef<HTMLInputElement | null>(null);
  const list = useRef<HTMLDivElement | null>(null);
  const groups = useMemo(() => searchCommands(entries, query, recent), [entries, query, recent]);
  const flat = groups.flatMap(g => g.items);
  useEffect(() => { input.current?.focus(); }, []);
  useEffect(() => { setActive(0); }, [query]);
  useEffect(() => {
    list.current?.querySelector('[data-active="true"]')?.scrollIntoView({ block: 'nearest' });
  }, [active]);
  const run = (entry: CommandEntry | undefined) => {
    if (!entry || entry.disabled) return;
    const next = [entry.id, ...recent.filter(id => id !== entry.id)].slice(0, 20);
    setRecent(next);
    try { window.localStorage.setItem(RECENT_KEY, JSON.stringify(next)); } catch { /* private mode */ }
    onClose();
    entry.run();
  };
  const prefix = query.trimStart()[0];
  const activeFilter = prefix === '>' || prefix === '@' || prefix === '#' ? prefix : '';
  let index = -1;
  return <div className="command-palette__backdrop" data-native-overlay onMouseDown={event => { if (event.target === event.currentTarget) onClose(); }}>
    <section className="command-palette" role="dialog" aria-modal="true" aria-label="Command palette"
      onKeyDown={event => {
        if (event.key === 'Escape') { event.preventDefault(); onClose(); }
        else if (event.key === 'ArrowDown') { event.preventDefault(); setActive(a => Math.min(flat.length - 1, a + 1)); }
        else if (event.key === 'ArrowUp') { event.preventDefault(); setActive(a => Math.max(0, a - 1)); }
        else if (event.key === 'Enter') { event.preventDefault(); run(flat[active]); }
      }}>
      <div className="command-palette__search">
        <Search size={17} aria-hidden="true"/>
        <input ref={input} value={query} onChange={e => setQuery(e.target.value)}
          placeholder="Search commands, windows, entities, assets…" aria-label="Command"
          aria-controls="command-palette-list" aria-activedescendant={flat[active] ? 'cmd-' + flat[active].id : undefined}/>
        <div className="command-palette__filters">
          {FILTERS.map(f => <button key={f.label} type="button" className={activeFilter === f.prefix ? 'is-active' : ''}
            onClick={() => { const text = query.replace(/^\s*[>@#]\s*/, ''); setQuery(f.prefix ? f.prefix + ' ' + text : text); input.current?.focus(); }}>{f.label}</button>)}
        </div>
      </div>
      <div className="command-palette__list" id="command-palette-list" role="listbox" ref={list}>
        {groups.length === 0 && <div className="command-palette__empty">Nothing found for “{query}”.</div>}
        {groups.map(group => <div key={group.kind} role="group" aria-label={group.title}>
          <div className="command-palette__group">{group.title}</div>
          {group.items.map(entry => {
            index += 1;
            const i = index;
            return <div key={entry.id} id={'cmd-' + entry.id} role="option" aria-selected={i === active} aria-disabled={entry.disabled}
              data-active={i === active} className={'command-palette__item' + (entry.disabled ? ' is-disabled' : '')}
              onMouseMove={() => setActive(i)} onClick={() => run(entry)}>
              <span className="command-palette__icon" style={{ background: KIND_COLORS[entry.kind] }} aria-hidden="true"/>
              <span className="command-palette__text"><b>{entry.title}</b>{entry.hint && <small>{entry.hint}</small>}</span>
              {entry.legacy && <span className="command-palette__legacy">legacy</span>}
              {entry.shortcut && <kbd>{entry.shortcut}</kbd>}
            </div>;
          })}
        </div>)}
      </div>
      <div className="command-palette__footer"><span>↑↓ navigate</span><span>↵ run</span><span>esc close</span>
        <span className="command-palette__spacer"/><span>&gt; commands · @ scene · # assets</span></div>
    </section>
  </div>;
}
