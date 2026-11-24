/** AIVERSE Studio UI: stable, genre-independent visual building blocks.
 * Import from "../ui" (path relative to panel). UI state lives in React;
 * engine data/actions MUST go via typed ../bridge, never via these components.
 */
import type {
  ReactNode, HTMLAttributes, ButtonHTMLAttributes, InputHTMLAttributes,
  SelectHTMLAttributes, TextareaHTMLAttributes, LabelHTMLAttributes
} from 'react';

import { StudioLoader } from './StudioLoader';

const classes = (...values: Array<string | undefined | false>) => values.filter(Boolean).join(' ');
export type StudioTone = 'neutral' | 'accent' | 'success' | 'warning' | 'danger';
export type StudioButtonVariant = 'secondary' | 'primary' | 'ghost' | 'danger';
export type StudioControlSize = 'sm' | 'md' | 'lg';

export interface StudioPanelProps extends HTMLAttributes<HTMLElement> {
  title: string;
  eyebrow?: string;
  subtitle?: string;
  actions?: ReactNode;
  children?: ReactNode;
  flush?: boolean;
  footer?: ReactNode;
}
/** Full-height, dock-safe pane. It never owns an engine viewport or input. */
export function StudioPanel({ title, eyebrow, subtitle, actions, footer, flush=false,
  className, children, ...props }: StudioPanelProps) {
  return <section {...props} className={classes('au-panel',className)}>
    <header className="au-panel__header">
      <div className="au-panel__heading">
        {eyebrow && <div className="au-panel__eyebrow">{eyebrow}</div>}
        <h2 className="au-panel__title">{title}</h2>
        {subtitle && <p className="au-panel__subtitle">{subtitle}</p>}
      </div>
      {actions && <div className="au-panel__actions">{actions}</div>}
    </header>
    <div className={classes('au-panel__body',flush && 'au-panel__body--flush')}>{children}</div>
    {footer && <footer className="au-panel__footer">{footer}</footer>}
  </section>;
}

export interface StudioButtonProps extends ButtonHTMLAttributes<HTMLButtonElement> {
  variant?: StudioButtonVariant;
  size?: StudioControlSize;
  loading?: boolean;
}
export function StudioButton({ variant='secondary',size='md',loading=false,disabled,
  className,children,type='button',...props }:StudioButtonProps) {
  return <button {...props} type={type} disabled={disabled || loading}
    aria-busy={loading || undefined}
    className={classes('au-button',variant!=='secondary' && 'au-button--'+variant,
      size!=='md' && 'au-button--'+size,loading && 'au-button--loading',className)}>
    {loading && <StudioLoader size={16} aria-hidden="true"/>}
    {children}
  </button>;
}
export interface StudioIconButtonProps extends Omit<StudioButtonProps,'children'> {
  'aria-label': string; icon: ReactNode;
}
export function StudioIconButton({ icon, className, ...props }: StudioIconButtonProps) {
  return <StudioButton {...props} className={classes('au-button--icon',className)}>{icon}</StudioButton>;
}
export interface StudioSectionProps extends HTMLAttributes<HTMLElement> {
  title: string; actions?: ReactNode; children?: ReactNode;
}
export function StudioSection({title,actions,className,children,...props}:StudioSectionProps) {
  return <section {...props} className={classes('au-section',className)}>
    <header className="au-section__header">
      <h3 className="au-section__title">{title}</h3>
      {actions && <div className="au-section__actions">{actions}</div>}
    </header>{children}
  </section>;
}
export interface StudioFieldProps extends Omit<LabelHTMLAttributes<HTMLLabelElement>,'htmlFor'> {
  label: string; htmlFor: string; hint?: string; error?: string;
  children: ReactNode;
}
export function StudioField({label,htmlFor,hint,error,className,children,...props}:StudioFieldProps) {
  return <div className={classes('au-field',className)}>
    <label {...props} htmlFor={htmlFor} className="au-field__label">{label}</label>
    {children}
    {hint && <small className="au-field__hint">{hint}</small>}
    {error && <small className="au-field__error" role="alert">{error}</small>}
  </div>;
}
export function StudioInput({className,...props}:InputHTMLAttributes<HTMLInputElement>) {
  return <input {...props} className={classes('au-input',className)}/>;
}
export function StudioSelect({className,children,...props}:SelectHTMLAttributes<HTMLSelectElement>) {
  return <select {...props} className={classes('au-select',className)}>{children}</select>;
}
export function StudioTextarea({className,...props}:TextareaHTMLAttributes<HTMLTextAreaElement>) {
  return <textarea {...props} className={classes('au-textarea',className)}/>;
}
export interface StudioCheckboxProps extends InputHTMLAttributes<HTMLInputElement> {
  label: string;
}
export function StudioCheckbox({label,className,...props}:StudioCheckboxProps) {
  return <label className={classes('au-checkbox',className)}>
    <input {...props} type="checkbox" className="au-checkbox__input"/>
    <span>{label}</span>
  </label>;
}
export interface StudioBadgeProps extends HTMLAttributes<HTMLSpanElement> {
  tone?: StudioTone; children:ReactNode;
}
export function StudioBadge({tone='neutral',className,children,...props}:StudioBadgeProps) {
  return <span {...props} className={classes('au-badge',tone!=='neutral' && 'au-badge--'+tone,className)}>{children}</span>;
}
export interface StudioAlertProps extends HTMLAttributes<HTMLDivElement> {
  tone?: Extract<StudioTone,'neutral'|'success'|'warning'|'danger'>;
  title?: string; children:ReactNode;
}
export function StudioAlert({tone='neutral',title,className,children,...props}:StudioAlertProps) {
  return <div {...props} role={tone==='danger'?'alert':'status'}
    className={classes('au-alert',tone!=='neutral'&&'au-alert--'+tone,className)}>
    <div className="au-alert__content">
      {title && <div className="au-alert__title">{title}</div>}
      {children}
    </div>
  </div>;
}
export interface StudioEmptyStateProps extends HTMLAttributes<HTMLDivElement> {
  title:string; description?:string; icon?:ReactNode; action?:ReactNode;
}
export function StudioEmptyState({title,description,icon,action,className,...props}:StudioEmptyStateProps) {
  return <div {...props} className={classes('au-empty',className)}>
    {icon && <div className="au-empty__icon" aria-hidden="true">{icon}</div>}
    <h3 className="au-empty__title">{title}</h3>
    {description && <p className="au-empty__description">{description}</p>}
    {action && <div className="au-empty__action">{action}</div>}
  </div>;
}
export function StudioSkeleton({width='100%',height=14,className,...props}:
  HTMLAttributes<HTMLSpanElement>&{width?:string|number;height?:string|number}) {
  return <span {...props} aria-hidden="true" className={classes('au-skeleton',className)} style={{
    width,height,...props.style
  }}/>;
}
export interface StudioTabsProps<T extends string> {
  items: ReadonlyArray<{id:T;label:string;disabled?:boolean}>;
  selected:T; onChange:(value:T)=>void; label:string; className?:string;
}
export function StudioTabs<T extends string>({items,selected,onChange,label,className}:StudioTabsProps<T>) {
  return <div role="tablist" aria-label={label} className={classes('au-tabs',className)}>
    {items.map(item=><button key={item.id} type="button" role="tab"
      className="au-tab" disabled={item.disabled} aria-selected={selected===item.id}
      onClick={()=>onChange(item.id)}>{item.label}</button>)}
  </div>;
}
export interface StudioListRowProps extends ButtonHTMLAttributes<HTMLButtonElement> {
  selected?:boolean; leading?:ReactNode; label:string; meta?:ReactNode;
}
export function StudioListRow({selected=false,leading,label,meta,className,...props}:StudioListRowProps) {
  return <button {...props} type="button" aria-selected={selected}
    className={classes('au-list-row',className)}>
    {leading}{<span className="au-list-row__label" title={label}>{label}</span>}
    {meta && <span className="au-list-row__meta">{meta}</span>}
  </button>;
}
export interface StudioProgressProps {
  value:number; max?:number; label:string; className?:string;
}
export function StudioProgress({value,max=100,label,className}:StudioProgressProps) {
  const safeMax=Number.isFinite(max)&&max>0?max:100;
  const safe=Number.isFinite(value)?Math.min(100,Math.max(0,value/safeMax*100)):0;
  return <div role="progressbar" aria-label={label} aria-valuemin={0} aria-valuemax={safeMax}
    aria-valuenow={Math.min(safeMax,Math.max(0,Number.isFinite(value)?value:0))}
    className={classes('au-progress',className)}>
    <div className="au-progress__bar" style={{width:safe+'%'}}/>
  </div>;
}
