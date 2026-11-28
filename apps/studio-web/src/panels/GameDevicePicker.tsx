import { useId, useRef, useState } from 'react';
import { Check, ChevronDown, X } from 'lucide-react';
import type { StudioGameDeviceOption } from '../types';
import { StudioButton, StudioDialog, StudioField, StudioInput } from '../ui';

function displayName(device: StudioGameDeviceOption) {
  return device.id === 'none' ? 'Free size' : device.name;
}

export default function GameDevicePicker({ devices, selected, disabled, onSelect }: {
  devices: StudioGameDeviceOption[];
  selected: string;
  disabled: boolean;
  onSelect: (id: string) => void;
}) {
  const triggerId = useId();
  const results = useRef<HTMLDivElement>(null);
  const [open, setOpen] = useState(false);
  const [query, setQuery] = useState('');
  const current = devices.find(device => device.id === selected);
  const groups = new Map<string, StudioGameDeviceOption[]>();
  const filter = query.trim().toLowerCase();
  for (const device of devices) {
    if (![displayName(device), device.name, device.id, device.group].some(value => value.toLowerCase().includes(filter))) continue;
    const label = device.group || 'Viewport';
    if (!groups.has(label)) groups.set(label, []);
    groups.get(label)!.push(device);
  }
  return <>
    <StudioField label="Device" htmlFor={triggerId}>
      <StudioButton id={triggerId} aria-label="Game device" aria-haspopup="dialog" aria-expanded={open}
        disabled={disabled || !devices.length} onClick={() => { setQuery(''); setOpen(true); }}>
        {current ? displayName(current) : 'Free size'} <ChevronDown size={14}/>
      </StudioButton>
    </StudioField>
    {open && <StudioDialog className="game-device-dialog" label="Choose a game device" onDismiss={() => setOpen(false)}>
      <div className="game-device-card">
        <header><h2>Choose a device</h2><StudioButton aria-label="Close device chooser" onClick={() => setOpen(false)}><X size={16}/></StudioButton></header>
        <StudioInput data-dialog-autofocus type="search" aria-label="Search devices" placeholder="Search devices or families"
          value={query} onChange={event => setQuery(event.target.value)}
          onKeyDown={event => {
            if (event.key !== 'ArrowDown' && event.key !== 'Enter') return;
            const first = results.current?.querySelector<HTMLButtonElement>('button:enabled');
            if (!first || event.nativeEvent.isComposing) return;
            event.preventDefault();
            if (event.key === 'Enter') first.click();
            else first.focus();
          }}/>
        <div className="game-device-results" ref={results} onKeyDown={event => {
          if (event.key !== 'ArrowDown' && event.key !== 'ArrowUp') return;
          if (!(event.target instanceof HTMLButtonElement)) return;
          const buttons = Array.from(event.currentTarget.querySelectorAll<HTMLButtonElement>('button:enabled'));
          const index = buttons.indexOf(event.target);
          if (index < 0) return;
          event.preventDefault();
          buttons[index + (event.key === 'ArrowDown' ? 1 : -1)]?.focus();
        }}>
          {Array.from(groups, ([label, options]) => <section key={label} aria-label={label}>
            <h3>{label}</h3>
            {options.map(device => <button type="button" className="game-device-row" key={device.id}
              disabled={disabled} aria-pressed={selected === device.id} onClick={() => { setOpen(false); onSelect(device.id); }}>
              <span>{displayName(device)}</span>
              <small>{device.width > 0 && `${device.width} × ${device.height} · ${device.scale}×`}</small>
              <span>{selected === device.id && <Check size={15}/>}</span>
            </button>)}
          </section>)}
          {!groups.size && <p role="status">No devices match your search.</p>}
        </div>
      </div>
    </StudioDialog>}
  </>;
}
