import { useEffect, useMemo, useRef, useState, type DragEvent } from 'react';
import { Link2, Search, X } from 'lucide-react';
import type { FieldOption } from '../propertyLogic';
import './EntityReferencePicker.css';

const ENTITY_DRAG_MIME = 'application/x-aiverse-entity';
interface Props {
  label: string;
  value: string;
  options: ReadonlyArray<FieldOption>;
  disabled?: boolean;
  onChange: (value: string) => void;
}

/** Studio's stable map-object selector, not an asset picker or live ECS ID editor.
 * Options are supplied by the native Inspector; writes are revalidated in C++.
 * Dragging from Hierarchy accepts ONLY entity:@stable-document-ID.
 */
export default function EntityReferencePicker({
  label, value, options, disabled = false, onChange,
}: Props) {
  const root = useRef<HTMLDivElement | null>(null);
  const searchRef = useRef<HTMLInputElement | null>(null);
  const [open, setOpen] = useState(false);
  const [query, setQuery] = useState('');
  const [error, setError] = useState('');
  const selected = options.find(item => String(item.value) === value);
  const items = useMemo(() => {
    const q = query.toLocaleLowerCase().trim();
    return options.filter(option => !q || option.label.toLocaleLowerCase().includes(q) ||
      String(option.value).toLocaleLowerCase().includes(q)).slice(0, 300);
  }, [options, query]);
  useEffect(() => {
    if (!open) return;
    searchRef.current?.focus();
    const outside = (event: PointerEvent) => {
      if (root.current && !root.current.contains(event.target as Node)) setOpen(false);
    };
    const escape = (event: KeyboardEvent) => {
      if (event.key === 'Escape') { event.preventDefault(); setOpen(false); }
    };
    window.addEventListener('pointerdown', outside);
    window.addEventListener('keydown', escape);
    return () => {
      window.removeEventListener('pointerdown', outside);
      window.removeEventListener('keydown', escape);
    };
  }, [open]);
  const choose = (id: string) => {
    if (disabled || !options.some(option => String(option.value) === id)) {
      setError('That object is no longer available.');
      return;
    }
    setError('');
    setOpen(false);
    if (id !== value) onChange(id);
  };
  const dragOver = (event: DragEvent<HTMLDivElement>) => {
    if (!disabled && event.dataTransfer.types.includes(ENTITY_DRAG_MIME)) {
      event.preventDefault();
      event.stopPropagation();
      event.dataTransfer.dropEffect = 'copy';
    }
  };
  const drop = (event: DragEvent<HTMLDivElement>) => {
    if (disabled || !event.dataTransfer.types.includes(ENTITY_DRAG_MIME)) return;
    event.preventDefault();
    event.stopPropagation();
    const raw = event.dataTransfer.getData(ENTITY_DRAG_MIME);
    if (!raw.startsWith('entity:@')) {
      setError('Choose a stable authored map entity, not a temporary index.');
      return;
    }
    choose(raw.slice('entity:@'.length));
  };
  return <div ref={root} className="studio-entity-ref" onDragOver={dragOver} onDrop={drop}>
    <div className="studio-entity-ref__control">
      <input className="au-input" readOnly disabled={disabled}
        value={selected?.label ?? (value || '')}
        placeholder="Drop or choose an object" aria-label={label}
        title={value ? 'Stable map ID: ' + value : 'No object linked'} />
      <button type="button" className="studio-entity-ref__open"
        disabled={disabled} aria-label={'Choose ' + label}
        aria-expanded={open} onClick={() => { setError(''); setQuery(''); setOpen(v => !v); }}>
        <Link2 size={13} />
      </button>
      {value && !disabled && <button type="button" className="studio-entity-ref__clear"
        aria-label={'Clear ' + label} onClick={() => choose('')}><X size={12}/></button>}
    </div>
    {error && <small className="studio-entity-ref__error" role="alert">{error}</small>}
    {open && <div className="studio-entity-ref__popover">
      <label className="studio-entity-ref__search">
        <Search size={12} aria-hidden="true"/>
        <input ref={searchRef} className="au-input" value={query}
          onChange={event => setQuery(event.target.value)}
          aria-label={'Search objects for ' + label}
          placeholder="Search map objects…"/>
      </label>
      <div className="studio-entity-ref__list" role="listbox" aria-label={'Objects for ' + label}>
        {items.map(option => <button type="button" role="option"
          aria-selected={String(option.value) === value}
          key={String(option.value)} title={String(option.value)}
          className="studio-entity-ref__option"
          onClick={() => choose(String(option.value))}>
          {option.label}
        </button>)}
        {!items.length && <div className="studio-entity-ref__empty">No matching objects.</div>}
      </div>
    </div>}
  </div>;
}
