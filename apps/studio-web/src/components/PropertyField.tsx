import type React from 'react';
import { useEffect, useRef, useState } from 'react';
import type { StudioProperty } from '../types';
import AssetReferencePicker from './AssetReferencePicker';
import { isSameFieldValue, parseFieldDraft, scrubValue, validateBooleanChange } from '../propertyLogic';

interface PropertyFieldProps {
  property: StudioProperty;
  disabled: boolean;
  onChange: (value: string | number | boolean) => void;
  /** 'axis': compact single-letter label for X/Y/Z vector rows. */
  variant?: 'row' | 'axis';
  className?: string;
}

/** Controlled, schema-driven property editor; writes happen only after validation. */
export default function PropertyField({ property, disabled, onChange, variant = 'row', className }: PropertyFieldProps) {
  const [draft, setDraft] = useState(property.mixed ? '' : String(property.value));
  const [error, setError] = useState('');
  const skipBlur = useRef(false);
  const locked = disabled || property.editable === false;

  useEffect(() => {
    setDraft(property.mixed ? '' : String(property.value));
    setError('');
  }, [property.key, property.value, property.mixed]);

  const validateAndCommit = (text: string) => {
    // Mixed multi-selection value left empty: nothing to write.
    if (property.mixed && text.trim() === '') { setError(''); return; }
    const parsed = parseFieldDraft(property, text);
    if (!parsed.ok) { setError(parsed.error); return; }
    setError('');
    if (property.mixed || !isSameFieldValue(property.value, parsed.value)) onChange(parsed.value);
  };

  // Hooks must run on every render, before the type-specific early returns:
  // the same row can switch type (e.g. a legacy unit's Mesh becomes an asset field).
  // Drag the label (Unity "scrub"): 1 px = 1 step; Shift = fine, Alt = snap
  // (15° for rotations, 1 unit otherwise). One commit on release = one undo step.
  const scrubbable = property.type === 'number' && !property.options?.length && !locked;
  const scrub = useRef<{ x: number; start: number; value: number; moved: boolean } | null>(null);
  const scrubHandlers = scrubbable ? {
    onPointerDown: (event: React.PointerEvent<HTMLSpanElement>) => {
      if (event.button !== 0) return;
      const start = Number(draft.trim() === '' ? property.value : draft); // mixed: starts from the primary object
      if (!Number.isFinite(start)) return;
      event.preventDefault();
      event.currentTarget.setPointerCapture(event.pointerId);
      scrub.current = { x: event.clientX, start, value: start, moved: false };
    },
    onPointerMove: (event: React.PointerEvent<HTMLSpanElement>) => {
      const s = scrub.current;
      if (!s) return;
      const dx = event.clientX - s.x;
      if (!s.moved && Math.abs(dx) < 2) return;
      s.moved = true;
      const rotation = /^rotation|^yaw$|^facing$/.test(property.key);
      s.value = scrubValue(s.start, dx, { step: property.step ?? (rotation ? 1 : undefined), min: property.min, max: property.max,
        integer: property.integer, fine: event.shiftKey, snap: event.altKey ? (rotation ? 15 : 1) : undefined });
      setDraft(String(s.value)); setError('');
    },
    onPointerUp: (event: React.PointerEvent<HTMLSpanElement>) => {
      const s = scrub.current;
      scrub.current = null;
      if (event.currentTarget.hasPointerCapture(event.pointerId)) event.currentTarget.releasePointerCapture(event.pointerId);
      if (s?.moved) validateAndCommit(String(s.value));
    },
    onPointerCancel: () => { scrub.current = null; setDraft(String(property.value)); },
    // A click on a <label> focuses its input; a scrub must not.
    onClick: (event: React.MouseEvent) => { event.preventDefault(); },
    title: 'Drag to change · Shift fine · Alt snap',
  } : {};
  if (property.type === 'asset') {
    return <div className="property-row">
      <span>{property.label}</span>
      <AssetReferencePicker label={property.label} value={String(property.value)}
        kinds={property.assetKinds} disabled={locked}
        onChange={value => { if (value !== property.value) onChange(value); }}/>
      {property.description && <small className="property-description">{property.description}</small>}
    </div>;
  }

  if (property.type === 'boolean') {
    return <label className="property-row">
      <span>{property.label}</span>
      <input type="checkbox" className="au-checkbox__input" disabled={locked} checked={!property.mixed && Boolean(property.value)}
        ref={el => { if (el) el.indeterminate = !!property.mixed; }}
        aria-label={property.label}
        onChange={event => {
          const next = validateBooleanChange(property, event.target.checked);
          if (next.ok && !isSameFieldValue(property.value, next.value)) onChange(next.value);
        }}/>
      {property.description && <small className="property-description au-field__hint">{property.description}</small>}
    </label>;
  }

  return <label className={'property-row' + (variant === 'axis' ? ' property-row--axis' : '') + (className ? ' ' + className : '')}>
    <span className={scrubbable ? 'property-scrub' : undefined} {...scrubHandlers}>{property.label}</span>
    {property.options?.length ?
      <select className="au-select" disabled={locked} value={property.mixed ? '\u0000mixed' : String(property.value)}
        aria-label={property.label}
        onChange={event => validateAndCommit(event.target.value)}>
        {property.mixed && <option value={'\u0000mixed'} disabled>—</option>}
        {property.options.map(option =>
          <option key={String(option.value)} value={String(option.value)}>{option.label}</option>)}
      </select>
      : <input
          className="au-input"
          disabled={locked}
          type={property.type === 'number' ? 'number' : 'text'}
          value={draft}
          placeholder={property.mixed ? '—' : undefined}
          min={property.type === 'number' ? property.min : undefined}
          max={property.type === 'number' ? property.max : undefined}
          step={property.type === 'number' ? (property.step ?? 'any') : undefined}
          aria-label={property.label}
          aria-invalid={error ? true : undefined}
          onChange={event => { setDraft(event.target.value); setError(''); }}
          onBlur={() => {
            if (skipBlur.current) { skipBlur.current = false; return; }
            if (!locked) validateAndCommit(draft);
          }}
          onKeyDown={event => {
            if (event.key === 'Enter') event.currentTarget.blur();
            if (event.key === 'Escape') {
              skipBlur.current = true;
              setDraft(String(property.value));
              setError('');
              event.currentTarget.blur();
            }
          }}
        />}
    {error && <small className="property-error au-field__error" role="alert">{error}</small>}
    {property.description && <small className="property-description">{property.description}</small>}
  </label>;
}
