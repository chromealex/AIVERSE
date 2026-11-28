import { useEffect, useId, useRef, useState } from 'react';

export default function GameSpeedSlider({ value, disabled, onCommit }: {
  value: number; disabled: boolean; onCommit: (value: number) => void;
}) {
  const id = useId();
  const input = useRef<HTMLInputElement>(null);
  const [draft, setDraft] = useState<number | null>(null);
  const speed = draft ?? value;
  const decode = (position: number) => Math.round(100 * 2 ** position) / 100;

  // The native change event commits a drag on release and a keyboard step immediately.
  useEffect(() => {
    const slider = input.current;
    if (!slider) return;
    const commit = () => {
      const next = decode(slider.valueAsNumber);
      setDraft(null);
      if (!disabled && next !== value) onCommit(next);
    };
    slider.addEventListener('change', commit);
    return () => slider.removeEventListener('change', commit);
  }, [value, disabled, onCommit]);

  return <div className="studio-game__speed">
    <label htmlFor={id}>Simulation speed</label>
    <input id={id} ref={input} type="range" min="-2" max="4" step="0.125" disabled={disabled}
      value={Math.max(-2, Math.min(4, Math.log2(speed)))} aria-valuetext={speed + '×'}
      onInput={event => setDraft(decode(event.currentTarget.valueAsNumber))}
      onPointerCancel={() => setDraft(null)}/>
    <output htmlFor={id}>{speed}×</output>
  </div>;
}
