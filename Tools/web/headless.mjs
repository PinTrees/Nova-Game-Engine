// 웹 빌드 검사: 창 없는 Chrome (별도 프로필) 을 DevTools 프로토콜로 제어 — 화면 · 마우스를 건드리지 않는다
//   node Tools/web/headless.mjs <url> [--wait 8] [--shot out.png] [--eval "식"] [--size 1280x720] [--until "식"]
//   출력 (JSON): { console: [...], errors: [...], eval: 값, title }
import { spawn } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';

const args = process.argv.slice(2);
const url = args[0];
const opt = (name, d) => { const i = args.indexOf('--' + name); return i >= 0 ? args[i + 1] : d; };
const waitSec = Number(opt('wait', '8'));
const shot = opt('shot', '');
const evalExpr = opt('eval', '');
const until = opt('until', '');
const [w, h] = opt('size', '1280x720').split('x').map(Number);
const chrome = process.env.NOVA_CHROME || [
    'C:/Program Files/Google/Chrome/Application/chrome.exe',
    'C:/Program Files (x86)/Google/Chrome/Application/chrome.exe',
    'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe',
].find(p => fs.existsSync(p));
const profile = path.join(os.homedir(), '.nova', 'chrome-headless');
fs.mkdirSync(profile, { recursive: true });
const port = 9300 + Math.floor(Math.random() * 500);

const proc = spawn(chrome, [
    '--headless=new', `--remote-debugging-port=${port}`, `--user-data-dir=${profile}`, `--window-size=${w},${h}`,
    '--enable-unsafe-webgpu', '--enable-features=Vulkan', '--use-angle=d3d11', '--no-first-run', '--no-default-browser-check',
    // 소리: 입력 없이 재생 (검사가 출력 진폭을 읽는다) · 스피커로는 내지 않는다 (사용자가 같은 PC 를 쓴다)
    '--autoplay-policy=no-user-gesture-required', '--mute-audio',
    'about:blank'], { stdio: 'ignore' });

const sleep = ms => new Promise(r => setTimeout(r, ms));
let ws, nextId = 1;
const pending = new Map();
const out = { console: [], errors: [] };

async function connect() {
    for (let i = 0; i < 50; i++) {
        try {
            const list = await (await fetch(`http://127.0.0.1:${port}/json/list`)).json();
            const page = list.find(t => t.type === 'page');
            if (page) return page.webSocketDebuggerUrl;
        } catch { }
        await sleep(200);
    }
    throw new Error('chrome did not start');
}
function send(method, params = {}) {
    const id = nextId++;
    ws.send(JSON.stringify({ id, method, params }));
    return new Promise((res, rej) => pending.set(id, { res, rej }));
}

try {
    ws = new WebSocket(await connect());
    await new Promise(r => ws.addEventListener('open', r));
    ws.addEventListener('message', ev => {
        const m = JSON.parse(ev.data);
        if (m.id && pending.has(m.id)) { const p = pending.get(m.id); pending.delete(m.id); m.error ? p.rej(new Error(m.error.message)) : p.res(m.result); return; }
        if (m.method === 'Runtime.consoleAPICalled')
            out.console.push(`[${m.params.type}] ` + m.params.args.map(a => a.value ?? a.description ?? '').join(' '));
        else if (m.method === 'Runtime.exceptionThrown')
            out.errors.push(m.params.exceptionDetails.exception?.description || m.params.exceptionDetails.text);
        else if (m.method === 'Log.entryAdded')
            out.console.push(`[${m.params.entry.level}] ${m.params.entry.text}`);
    });
    await send('Runtime.enable');
    await send('Log.enable');
    await send('Page.enable');
    await send('Page.navigate', { url });
    const t0 = Date.now();
    while (Date.now() - t0 < waitSec * 1000) {
        await sleep(250);
        if (until) {
            const r = await send('Runtime.evaluate', { expression: until, returnByValue: true });
            if (r.result?.value) break;
        }
    }
    if (evalExpr) {
        const r = await send('Runtime.evaluate', { expression: evalExpr, returnByValue: true, awaitPromise: true });
        out.eval = r.result?.value ?? r.exceptionDetails?.exception?.description;
    }
    out.title = (await send('Runtime.evaluate', { expression: 'document.title', returnByValue: true })).result.value;
    if (shot) {
        const s = await send('Page.captureScreenshot', { format: 'png' });
        fs.writeFileSync(shot, Buffer.from(s.data, 'base64'));
        out.shot = shot;
    }
} catch (e) {
    out.errors.push('harness: ' + e.message);
} finally {
    console.log(JSON.stringify(out, null, 1));
    try { await Promise.race([send('Browser.close'), sleep(1500)]); } catch { }
    try { proc.kill(); } catch { }
    process.exit(0);
}
