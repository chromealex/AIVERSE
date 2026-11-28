import { useEffect, useState } from 'react';

const aliases: Record<string, string> = {
  BracketLeft: 'LeftBracket', BracketRight: 'RightBracket', Quote: 'Apostrophe', Backquote: 'GraveAccent',
  ShiftLeft: 'LeftShift', ShiftRight: 'RightShift', ControlLeft: 'LeftCtrl', ControlRight: 'RightCtrl',
  AltLeft: 'LeftAlt', AltRight: 'RightAlt', MetaLeft: 'LeftSuper', MetaRight: 'RightSuper',
};

export default function InputKeyCapture({keys, bind}: {keys: string[]; bind: (code: string) => void}) {
  const [waiting, setWaiting] = useState(false);
  useEffect(() => {
    if (!waiting) return;
    const cancel = () => setWaiting(false);
    const capture = (event: KeyboardEvent) => {
      event.preventDefault();
      event.stopImmediatePropagation();
      if (event.repeat) return;
      if (event.code === 'Escape') return cancel();
      const code = aliases[event.code] ?? event.code.replace(/^Key/, '').replace(/^Digit/, 'Num').replace(/^Arrow/, '');
      if (!keys.includes(code)) return;
      bind(code);
      cancel();
    };
    window.addEventListener('keydown', capture, true);
    window.addEventListener('blur', cancel);
    return () => { window.removeEventListener('keydown', capture, true); window.removeEventListener('blur', cancel); };
  }, [waiting, keys, bind]);
  return <button type="button" className="au-button au-button--sm" aria-pressed={waiting}
    onBlur={() => setWaiting(false)} onClick={() => setWaiting(!waiting)}>
    {waiting ? 'Press a key · Esc cancels' : 'Capture key'}
  </button>;
}
