// Small, consistent building blocks for Character Creator categories:
// a card with title + hint, a switch row, a segmented choice and a value slider.
import type { ReactNode } from 'react';

export function HcHeader({ title, hint }: { title: string; hint: string }) {
  return <header className="hc-head"><h2>{title}</h2><p>{hint}</p></header>;
}
export function HcCard({ title, hint, actions, children }: { title: string; hint?: string; actions?: ReactNode; children: ReactNode }) {
  return <section className="hc-card-box">
    <div className="hc-card-box__top"><div><h3>{title}</h3>{hint && <p>{hint}</p>}</div>{actions}</div>
    {children}
  </section>;
}
export function HcSwitch({ label, hint, checked, disabled, onChange }: { label: string; hint?: string; checked: boolean; disabled?: boolean; onChange: (v: boolean) => void }) {
  return <label className={'hc-switch' + (disabled ? ' hc-switch--off' : '')}>
    <span><strong>{label}</strong>{hint && <small>{hint}</small>}</span>
    <input type="checkbox" role="switch" checked={checked} disabled={disabled} onChange={e => onChange(e.target.checked)}/>
    <i aria-hidden="true"/>
  </label>;
}
export function HcSegment<T extends string>({ label, value, options, onChange, disabled }:
  { label: string; value: T; options: { id: T; label: string; icon?: ReactNode }[]; onChange: (v: T) => void; disabled?: boolean }) {
  return <div className="hc-segment" role="radiogroup" aria-label={label}>{options.map(o =>
    <button key={o.id} type="button" role="radio" aria-checked={value === o.id} disabled={disabled} onClick={() => onChange(o.id)}>{o.icon}{o.label}</button>)}</div>;
}
export function HcSlider({ id, label, min, max, step, value, format, disabled, onChange, onCommit }:
  { id: string; label: string; min: number; max: number; step: number; value: number; format?: (v: number) => string; disabled?: boolean;
    onChange: (v: number) => void; onCommit?: () => void }) {
  const pct = ((value - min) / (max - min)) * 100;
  return <div className="hc-vslider">
    <div className="hc-vslider__top"><label htmlFor={id}>{label}</label><output htmlFor={id}>{format ? format(value) : value.toFixed(2)}</output></div>
    <input id={id} type="range" min={min} max={max} step={step} value={value} disabled={disabled}
      style={{ ['--hc-fill' as string]: pct + '%' }}
      onChange={e => onChange(Number(e.target.value))} onPointerUp={onCommit} onKeyUp={onCommit}/>
  </div>;
}
export function HcNote({ tone = 'info', children }: { tone?: 'info' | 'warn' | 'error'; children: ReactNode }) {
  return <p className={'hc-note hc-note--' + tone} role={tone === 'error' ? 'alert' : 'status'}>{children}</p>;
}
