// 웹 빌드 미리 보기 서버: node Tools/web/serve.mjs <폴더> [포트=8600]
//  - .wasm = application/wasm, COOP/COEP (SharedArrayBuffer — 스레드 빌드용), 캐시 끔
import http from 'node:http';
import fs from 'node:fs';
import path from 'node:path';

const root = path.resolve(process.argv[2] || '.');
const port = Number(process.argv[3] || 8600);
const types = {
    '.html': 'text/html; charset=utf-8', '.js': 'text/javascript', '.mjs': 'text/javascript', '.wasm': 'application/wasm',
    '.json': 'application/json', '.data': 'application/octet-stream', '.png': 'image/png', '.webp': 'image/webp',
    '.css': 'text/css', '.dll': 'application/octet-stream', '.pdb': 'application/octet-stream', '.dat': 'application/octet-stream',
};

http.createServer((req, res) => {
    const url = decodeURIComponent(req.url.split('?')[0]);
    let file = path.join(root, url);
    if (!file.startsWith(root)) { res.writeHead(403); res.end(); return; }
    if (fs.existsSync(file) && fs.statSync(file).isDirectory()) file = path.join(file, 'index.html');
    fs.readFile(file, (err, data) => {
        if (err) { res.writeHead(404); res.end('not found'); return; }
        res.writeHead(200, {
            'Content-Type': types[path.extname(file).toLowerCase()] || 'application/octet-stream',
            'Cross-Origin-Opener-Policy': 'same-origin',
            'Cross-Origin-Embedder-Policy': 'require-corp',
            'Cache-Control': 'no-store',
        });
        res.end(data);
    });
}).listen(port, '127.0.0.1', () => console.log(`NOVA web: http://localhost:${port}/  (${root})`));
