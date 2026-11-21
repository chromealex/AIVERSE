import { Component, type ErrorInfo, type ReactNode } from 'react';

/** Keeps a failing panel from blanking the whole Studio: shows the error and a retry. */
export default class PanelErrorBoundary extends Component<{ name: string; children: ReactNode }, { error: Error | null }> {
  state: { error: Error | null } = { error: null };
  static getDerivedStateFromError(error: Error) { return { error }; }
  componentDidCatch(error: Error, info: ErrorInfo) { console.error('[studio] panel ' + this.props.name + ' failed', error, info.componentStack); }
  render() {
    if (!this.state.error) return this.props.children;
    return <div className="panel-error" role="alert">
      <strong>{this.props.name} failed</strong>
      <code>{this.state.error.message}</code>
      <button type="button" onClick={() => this.setState({ error: null })}>Reload panel</button>
    </div>;
  }
}
