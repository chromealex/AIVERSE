/** AIVERSE Studio UI: the loading indicator — the animated AIVERSE mark (blades slide in, the
 * diamond turns into place and pulses), the same one the engine, the site and the web builds show.
 * The markup is kept in sync by tools/branding/logo/loader.py; do not edit it by hand. */
import type { HTMLAttributes } from 'react';

const MARK =
  // aiverse-loader:begin (tools/branding/logo/loader.py)
"<svg class=\"aiv-mark\" viewBox=\"0 60 1000 880\" role=\"img\" aria-label=\"Loading\"><style>.aiv-mark{display:block;overflow:visible}.aiv-mark .aiv-b{fill:var(--aiv-mark-ink,#141210);opacity:0;animation:.55s cubic-bezier(.2,.7,.25,1) forwards}.aiv-mark .aiv-t{animation-name:aiv-t}.aiv-mark .aiv-l{animation-name:aiv-l;animation-delay:.1s}.aiv-mark .aiv-r{animation-name:aiv-r;animation-delay:.2s}.aiv-mark .aiv-d{fill:var(--aiv-mark-accent,#E9C3A4);opacity:0;transform-box:view-box;transform-origin:500px 588px;animation:aiv-d .5s .55s cubic-bezier(.2,.7,.25,1) forwards,aiv-p 1.6s 1.05s ease-in-out infinite}@keyframes aiv-t{from{opacity:0;transform:translate(0,-70px)}to{opacity:1;transform:none}}@keyframes aiv-l{from{opacity:0;transform:translate(-60px,32px)}to{opacity:1;transform:none}}@keyframes aiv-r{from{opacity:0;transform:translate(60px,32px)}to{opacity:1;transform:none}}@keyframes aiv-d{from{opacity:0;transform:rotate(-90deg) scale(.3)}to{opacity:1;transform:none}}@keyframes aiv-p{0%,100%{opacity:1;transform:none}50%{opacity:1;transform:scale(1.07)}}@media (prefers-reduced-motion:reduce){.aiv-mark .aiv-b,.aiv-mark .aiv-d{animation:none;opacity:1}}</style><polygon class=\"aiv-b aiv-t\" points=\"500,94 373,179 373,476 500,397 627,476 627,179\"/><polygon class=\"aiv-b aiv-l\" points=\"433,741 147,907 23,833 23,719 307,561 307,663\"/><polygon class=\"aiv-b aiv-r\" points=\"567,741 853,907 977,833 977,719 693,561 693,663\"/><polygon class=\"aiv-d\" points=\"500,438 350,588 500,738 650,588\"/></svg>"
  // aiverse-loader:end
  ;

export interface StudioLoaderProps extends HTMLAttributes<HTMLSpanElement> {
  /** Width of the mark in px (height follows). */
  size?: number;
  /** Optional caption under the mark. */
  label?: string;
}

export function StudioLoader({ size = 28, label, className, style, ...props }: StudioLoaderProps) {
  return <span {...props} role="status" aria-label={label ?? 'Loading'}
    className={['au-loader', label ? 'au-loader--labelled' : '', className].filter(Boolean).join(' ')}
    style={{ ...style, ['--au-loader-size' as string]: `${size}px` }}>
    <span className="au-loader__mark" aria-hidden="true" dangerouslySetInnerHTML={{ __html: MARK }}/>
    {label && <span className="au-loader__label">{label}</span>}
  </span>;
}
