import React from 'react';
import { createRoot } from 'react-dom/client';
import App from './App';
import './theme/tokens.css';
import './ui/studio-ui.css';
import './styles.css';
import './ui/editor-shell.css';
import './ui/first-concept.css';
import './ui/compact-shell.css';

const el = document.getElementById('root');
if (!el) throw new Error('AIVERSE Studio root not found');
createRoot(el).render(<React.StrictMode><App /></React.StrictMode>);
