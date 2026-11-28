import { useRef } from 'react';
import { StudioButton, StudioDialog } from '../ui';

function inline(text: string) {
  return text.split(/(`[^`\n]+`|\*\*[^*\n]+\*\*)/).map((part, i) => {
    if (part.startsWith('`') && part.endsWith('`')) return <code key={i}>{part.slice(1,-1)}</code>;
    if (part.startsWith('**') && part.endsWith('**')) return <strong key={i}>{part.slice(2,-2)}</strong>;
    return part;
  });
}

function ReportBlock({ text }: { text: string }) {
  const lines = text.split('\n');
  if (lines.every(line => line.startsWith('- '))) return <ul>{lines.map((line,i) => <li key={i}>{inline(line.slice(2))}</li>)}</ul>;
  if (lines.every(line => /^\d+\. /.test(line))) return <ol>{lines.map((line,i) => <li key={i}>{inline(line.replace(/^\d+\. /,''))}</li>)}</ol>;
  if (lines.every(line => line.startsWith('> '))) return <blockquote>{inline(lines.map(line => line.slice(2)).join('\n'))}</blockquote>;
  if (lines.length > 1 && lines.every(line => line.startsWith('|') && line.endsWith('|')) && /^\|[-: |]+\|$/.test(lines[1])) {
    const cells=(line: string) => line.slice(1,-1).split('|').map(value => value.trim());
    return <div className="agent__report-table"><table>
      <thead><tr>{cells(lines[0]).map((v,i) => <th key={i}>{inline(v)}</th>)}</tr></thead>
      <tbody>{lines.slice(2).map((line,i) => <tr key={i}>{cells(line).map((v,j) => <td key={j}>{inline(v)}</td>)}</tr>)}</tbody>
    </table></div>;
  }
  return <p>{inline(text)}</p>;
}

export default function WizardReport({ text, close }: { text: string; close: () => void }) {
  const summary = useRef<HTMLElement>(null);
  const sections = text.replaceAll('\r\n','\n').split(/\n(?=#{1,2} )/);
  const isSummary = (section: string) => /^## (?:Agent|Агент)(?:\s|:|$)/i.test(section);
  return <StudioDialog label="Game wizard report" className="agent__report" onDismiss={close}>
    <header><strong>WIZARD.md</strong>
      {sections.some(isSummary) && <StudioButton size="sm" onClick={() => summary.current?.focus()}>Agent summary</StudioButton>}
      <StudioButton size="sm" onClick={close}>Close</StudioButton></header>
    <article>{sections.map((section,i) => {
      const heading = /^(#{1,2}) ([^\n]*)(?:\n|$)/.exec(section);
      return <section key={i} ref={isSummary(section) ? summary : undefined} tabIndex={-1}>
        {heading && (heading[1] === '#' ? <h2>{heading[2]}</h2> : <h3>{heading[2]}</h3>)}
        {section.slice(heading?.[0].length ?? 0).trim().split(/\n\s*\n/).filter(Boolean).map((block,j) => <ReportBlock key={j} text={block}/>)}
      </section>;
    })}</article>
  </StudioDialog>;
}
