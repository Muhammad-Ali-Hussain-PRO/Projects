import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile, mkdtemp, writeFile, rm } from 'node:fs/promises';
import { join } from 'node:path';
import { tmpdir } from 'node:os';
import { pathToFileURL, fileURLToPath } from 'node:url';
import { build } from 'esbuild';
import { JSDOM } from 'jsdom';

const frontend = fileURLToPath(new URL('../', import.meta.url));
const publicRoot = new URL('../public/', import.meta.url);
const pause = ms => new Promise(resolve => setTimeout(resolve, ms));

async function waitFor(condition, label, timeout = 3000) {
  const started = Date.now();
  while (Date.now() - started < timeout) {
    const result = condition();
    if (result) return result;
    await pause(10);
  }
  throw new Error(`Timed out waiting for ${label}`);
}

test('React workbench renders all local samples, safe docs, and an authenticated backend flow', async t => {
  const projects = JSON.parse(await readFile(new URL('data/catalog.json', publicRoot), 'utf8'));
  assert.equal(projects.length, 10, 'all ten workspaces must be present');
  const dom = new JSDOM('<!doctype html><html><body><div id="root"></div></body></html>', {
    url: 'https://studio.example.org/', pretendToBeVisual: true,
  });
  const { window } = dom;
  const errors = [], fetched = [], descriptors = new Map(), backendRequests = [], storageWrites = [];
  let backendEnabled = false;
  const backendOrigin = 'https://api.studio.example.org';
  const sessionToken = 'ui-fixture-session-token';
  const replace = (name, value) => {
    descriptors.set(name, Object.getOwnPropertyDescriptor(globalThis, name));
    Object.defineProperty(globalThis, name, { configurable: true, writable: true, value });
  };
  const originalError = console.error;
  console.error = (...values) => { errors.push(values.map(String).join(' ')); };
  window.addEventListener('error', event => { errors.push(String(event.error || event.message)); event.preventDefault(); });
  window.scrollTo = () => {};
  window.HTMLElement.prototype.scrollIntoView = () => {};
  window.HTMLDialogElement.prototype.showModal = function () { this.setAttribute('open', ''); };
  const originalSetItem = window.Storage.prototype.setItem;
  window.Storage.prototype.setItem = function (key, value) {
    storageWrites.push({ key: String(key), value: String(value) });
    return originalSetItem.call(this, key, value);
  };
  const setValue = (element, value) => {
    assert.ok(element, 'controlled form input is missing');
    const prototype = element.tagName === 'INPUT' ? window.HTMLInputElement.prototype
      : element.tagName === 'TEXTAREA' ? window.HTMLTextAreaElement.prototype : window.HTMLSelectElement.prototype;
    Object.getOwnPropertyDescriptor(prototype, 'value').set.call(element, value);
    element.dispatchEvent(new window.Event('input', { bubbles: true }));
    element.dispatchEvent(new window.Event('change', { bubbles: true }));
  };
  // Only fixture asset reads are allowed. A local sample must not contact a
  // server, model provider, microphone, or search service.
  const fixtureFetch = async (input, options = {}) => {
    const url = new URL(typeof input === 'string' ? input : input.url || String(input), window.location.href);
    if (url.origin === backendOrigin) {
      assert.ok(backendEnabled, 'browser-local execution contacted the backend');
      backendRequests.push({ pathname: url.pathname, ...options });
      const response = value => new Response(JSON.stringify(value), { status: 200, headers: { 'Content-Type': 'application/json' } });
      if (url.pathname === '/api/health') return response({ projects: projects.map(p => p.id), provider_configured: false });
      if (url.pathname === '/api/metrics') {
        assert.equal(new Headers(options.headers).get('Authorization'), 'Bearer ' + sessionToken);
        assert.equal(options.redirect, 'error');
        return response({ requests: 0, source: 'test fixture' });
      }
      assert.equal(options.method, 'POST');
      assert.equal(new Headers(options.headers).get('Authorization'), 'Bearer ' + sessionToken);
      assert.equal(options.redirect, 'error');
      if (url.pathname === '/api/run/chat') {
        const payload = JSON.parse(options.body);
        assert.equal(payload.query, 'Where does Muhammad study?');
        assert.equal(payload.mode, 'local');
        return response({ status: 'ok', mode: 'local', answer_mode: 'extractive', supported: true,
          answer: 'Muhammad Ali Hussain studies B.S. Mathematics at University of Illinois Urbana-Champaign. [D1-P1-C1]',
          citations: [{ citation_id: 'D1-P1-C1', chunk_id: 'education-fixture', document_id: 'education', document_name: 'Education and academic record', page: 1,
            quote: 'Muhammad Ali Hussain studies B.S. Mathematics, Applied Mathematics concentration, at University of Illinois Urbana-Champaign.' }],
          evaluation: { metrics: { recall_at_k: 1, mrr: 1, latency_ms: 2.5 }, metric_scope: 'retrieval only' },
          warnings: [], notes: ['Connected backend response fixture; no provider was called.'] });
      }
      if (url.pathname === '/api/feedback') return response({ status: 'saved', feedback_id: 'fixture-feedback-1' });
      assert.fail(`unexpected backend endpoint ${url.pathname}`);
    }
    assert.equal(url.origin, window.location.origin, 'local sample attempted an external request');
    assert.ok(!options.method || options.method === 'GET', 'local sample attempted a server mutation');
    const pathname = url.pathname.replace(/^\/+/, '');
    assert.ok(pathname.startsWith('data/') || pathname.startsWith('docs/'), `unexpected asset ${pathname}`);
    fetched.push(pathname);
    let contents = await readFile(new URL(pathname, publicRoot));
    if (pathname === 'docs/rag.md') contents = Buffer.from(contents.toString('utf8') + '\n\n<script id="unsafe-doc">window.unsafeDocumentExecuted=true</script>\n\n[Unsafe target](javascript:alert(1))\n');
    return new Response(contents, { status: 200, headers: { 'Content-Type': pathname.endsWith('.json') ? 'application/json' : 'text/plain' } });
  };
  for (const [name, value] of Object.entries({
    window, document: window.document, navigator: window.navigator,
    history: window.history, location: window.location,
    HTMLElement: window.HTMLElement, Element: window.Element,
    Node: window.Node, Event: window.Event, MouseEvent: window.MouseEvent,
    requestAnimationFrame: window.requestAnimationFrame.bind(window),
    cancelAnimationFrame: window.cancelAnimationFrame.bind(window),
    fetch: fixtureFetch, IS_REACT_ACT_ENVIRONMENT: false,
  })) replace(name, value);
  window.fetch = fixtureFetch;
  const folder = await mkdtemp(join(tmpdir(), 'studio-ui-smoke-'));
  try {
    const bundled = await build({
      absWorkingDir: frontend, entryPoints: ['src/main.tsx'], bundle: true,
      format: 'esm', platform: 'browser', write: false, logLevel: 'silent',
      loader: { '.css': 'empty' },
      define: { 'import.meta.env.BASE_URL': JSON.stringify('/'), 'process.env.NODE_ENV': JSON.stringify('production') },
    });
    const bundlePath = join(folder, 'studio.mjs');
    await writeFile(bundlePath, bundled.outputFiles[0].text);
    await import(pathToFileURL(bundlePath).href);
    await waitFor(() => window.document.querySelectorAll('button.project-card').length === 10, 'ten catalog cards');
    assert.deepEqual(new Set(fetched), new Set(['data/catalog.json', 'data/portfolio.json', 'data/model.json', 'data/model-evaluation.json']));
    const expectedModes = {
      rag: 'browser_local_BM25', search: 'cached_fixture', voice: 'browser_audio',
      multimodal: 'browser_metadata', review: 'browser_static_heuristics',
      model: 'browser_trained_classifier', safety: 'browser_heuristics',
      analyst: 'browser_local_grammar', mcp: 'browser_protocol_explanation',
      chat: 'browser_local_BM25',
    };
    for (const project of projects) {
      await t.test(`${project.id}: open, run sample, inspect rendered result`, async () => {
        const card = window.document.querySelector(`button.project-card.card-${project.id}`);
        assert.ok(card, `missing catalog card ${project.id}`);
        card.click();
        await waitFor(() => window.document.querySelector('.workspace h1')?.textContent === project.title, `${project.id} workspace`);
        await pause(20); // Let the sample-reset effect populate the form.
        assert.equal(window.document.querySelector('.execution-toolbar select').value, 'browser');
        const runButton = window.document.querySelector('button.run-button');
        assert.ok(runButton && !runButton.disabled);
        runButton.click();
        const rendered = await waitFor(() => window.document.querySelector('.result-content .json-details pre'), `${project.id} result`);
        const result = JSON.parse(rendered.textContent);
        assert.equal(result.mode, expectedModes[project.id]);
        assert.notEqual(result.status, 'error');
        assert.ok(window.document.querySelector('.result-badges')?.textContent.includes(String(result.mode).replaceAll('_', ' ')));
        assert.equal(window.document.querySelector('[role="alert"]'), null, `${project.id} rendered an error alert`);
        if (project.id === 'rag' || project.id === 'chat') assert.ok(result.citations.length > 0);
        if (project.id === 'chat') assert.equal(window.document.querySelector('.feedback-form'), null, 'feedback must require a connected backend');
        if (project.id === 'analyst') {
          assert.deepEqual(Object.fromEntries(result.rows), { North: 2000, South: 2000, East: 1600, West: 1300 });
          assert.ok(window.document.querySelector('.table-scroll tbody tr'));
        }
        if (project.id === 'model') {
          assert.ok(result.prediction.ranking.length > 1);
          assert.ok(window.document.querySelector('.ranking .rank-row'));
        }
        if (project.id === 'safety') assert.equal(result.guard.decision, 'block');
        if (project.id === 'voice') assert.equal(result.status, 'requires_browser_permission');
        if (project.id === 'mcp') assert.equal(result.executed, false);
        window.document.querySelector('button.back-button').click();
        await waitFor(() => window.document.querySelectorAll('button.project-card').length === 10, `catalog after ${project.id}`);
      });
    }
    assert.equal(fetched.length, 4, 'local runs should use already-loaded assets');
    await t.test('documentation renders headings and code while escaping HTML and unsafe links', async () => {
      window.document.querySelector('button.project-card.card-rag').click();
      await waitFor(() => window.document.querySelector('.workspace h1')?.textContent === 'Atlas RAG', 'RAG workspace');
      await pause(20);
      window.document.querySelector('#tab-Documentation').click();
      const article = await waitFor(() => window.document.querySelector('.markdown-document h1')?.textContent === 'Document RAG module' && window.document.querySelector('.markdown-document'), 'rendered documentation');
      assert.ok(article.querySelector('pre code')?.textContent.includes('run'));
      assert.ok(article.querySelector('h2'));
      assert.equal(article.querySelector('script'), null);
      assert.equal(window.document.getElementById('unsafe-doc'), null);
      assert.equal(window.unsafeDocumentExecuted, undefined);
      assert.ok(article.textContent.includes('<script id="unsafe-doc">'));
      const unsafe = [...article.querySelectorAll('a')].find(a => a.textContent === 'Unsafe target');
      assert.equal(unsafe?.getAttribute('href'), '#');
      assert.ok([...article.querySelectorAll('a')].every(a => !a.getAttribute('href').startsWith('javascript:')));
      window.document.querySelector('button.back-button').click();
      await waitFor(() => window.document.querySelectorAll('button.project-card').length === 10, 'catalog after documentation');
    });
    await t.test('connection, chat, and explicit feedback carry bearer auth without persisting the token', async () => {
      backendEnabled = true;
      window.document.querySelector('button.connect-button').click();
      await waitFor(() => window.document.querySelector('dialog[open]'), 'connection dialog');
      setValue(window.document.querySelector('input[name="backend_url"]'), backendOrigin);
      setValue(window.document.querySelector('input[name="session_api_token"]'), sessionToken);
      await pause(10);
      window.document.querySelector('form[aria-label="Backend connection"] button.primary').click();
      await waitFor(() => !window.document.querySelector('dialog') && window.document.querySelector('button.connect-button')?.textContent.includes('Backend connected'), 'connected session');
      assert.ok(backendRequests.some(request => request.pathname === '/api/health'));
      assert.ok(backendRequests.some(request => request.pathname === '/api/metrics'), 'connection should verify the bearer token');
      window.document.querySelector('button.project-card.card-chat').click();
      await waitFor(() => window.document.querySelector('.workspace h1')?.textContent === 'Portfolio Copilot', 'chat workspace');
      await pause(20);
      setValue(window.document.querySelector('select[name="execution_environment"]'), 'backend');
      setValue(window.document.querySelector('textarea[name="primary_input"]'), 'Where does Muhammad study?');
      await pause(10);
      window.document.querySelector('button.run-button').click();
      await waitFor(() => window.document.querySelector('.feedback-form'), 'connected feedback form');
      assert.ok(window.document.querySelector('.answer-text')?.textContent.includes('University of Illinois'));
      assert.equal(window.document.querySelector('.citation-label')?.textContent, 'D1-P1-C1');
      assert.equal(window.document.querySelector('.metric-grid strong')?.textContent, '1');
      assert.equal(backendRequests.filter(request => request.pathname === '/api/feedback').length, 0, 'running chat must not auto-submit feedback');
      setValue(window.document.querySelector('select[name="feedback_rating"]'), '5');
      setValue(window.document.querySelector('textarea[name="feedback_comment"]'), 'Source citations made this answer easy to verify.');
      await pause(10);
      window.document.querySelector('.feedback-form button').click();
      await waitFor(() => window.document.querySelector('.feedback-form [role="status"]')?.textContent.includes('Feedback saved'), 'feedback confirmation');
      const feedback = backendRequests.find(request => request.pathname === '/api/feedback');
      assert.deepEqual(JSON.parse(feedback.body), { project: 'chat', rating: 5, comment: 'Source citations made this answer easy to verify.' });
      assert.equal(new Headers(feedback.headers).get('Authorization'), 'Bearer ' + sessionToken);
      assert.ok(storageWrites.every(write => !write.key.includes(sessionToken) && !write.value.includes(sessionToken)));
      assert.equal(window.localStorage.length, 0);
      assert.equal(window.sessionStorage.length, 0);
      assert.ok(!window.document.querySelector('.json-details pre').textContent.includes(sessionToken));
    });
    assert.equal(errors.length, 0, `React/browser errors: ${errors.join('\n')}`);
  } finally {
    await pause(10);
    window.close();
    console.error = originalError;
    for (const [name, descriptor] of descriptors) {
      if (descriptor) Object.defineProperty(globalThis, name, descriptor);
      else delete globalThis[name];
    }
    await rm(folder, { recursive: true, force: true });
  }
});
