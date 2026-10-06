#!/usr/bin/env bash
# 웹 게임 폴더를 미리 보기 서버로 띄우고 창 없는 Chrome 에서 돌려 캡처 + 진단 (CLI 만 — 화면 · 마우스를 쓰지 않는다)
#   Tools/web/run_scene.sh <게임 폴더> <캡처.png> [기다릴 프레임=60] [포트=8620]
#   출력 (JSON): phase · frames · fps · stats (그리기 · 패스 · 건너뜀) · gpuErrors · audio · 콘솔 오류 · scriptLog (C# Debug.Log) · size (캔버스)
set -e
DIR="$1"; SHOT="$2"; FRAMES="${3:-60}"; PORT="${4:-8620}"
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
node "$ROOT/Tools/web/serve.mjs" "$DIR" "$PORT" > /dev/null 2>&1 &
SERVER=$!
trap 'kill $SERVER 2>/dev/null || true' EXIT
for i in $(seq 1 40); do curl -s -o /dev/null "http://127.0.0.1:$PORT/game.json" && break; sleep 0.25; done
WIN_SHOT="$(cygpath -w "$SHOT" 2>/dev/null || echo "$SHOT")"
node "$ROOT/Tools/web/headless.mjs" "http://localhost:$PORT/" --wait 60 --size 1280x720 --shot "$WIN_SHOT" \
  --until "window.novaState && (window.novaState.phase=='error' || (window.novaState.phase=='running' && nova.frames() > $FRAMES))" \
  --eval "(async()=>{ if (!window.novaState || window.novaState.phase!=='running') return JSON.stringify(window.novaState||{}); const a=nova.frames(); await new Promise(r=>setTimeout(r,2000)); return JSON.stringify({ phase: window.novaState.phase, frames: nova.frames(), fps: (nova.frames()-a)/2, gpuErrors: window.novaState.gpuErrors, stats: nova.stats(), audio: nova.audio ? nova.audio() : null }) })()" \
  | python -c "
import sys, json
d = json.load(sys.stdin)
out = json.loads(d['eval']) if d.get('eval') and d['eval'].startswith('{') else {'eval': d.get('eval')}
out['consoleErrors'] = [l[:400] for l in d['console'] if l.startswith('[error]') and '404' not in l][:12]
out['engineWarnings'] = [l[:300] for l in d['console'] if '[WebGPU]' in l and 'effect ' not in l][:12]
out['exceptions'] = d['errors'][:3]
out['lastLog'] = [l[:200] for l in d['console'] if l.startswith('[warning] [')][-8:]
out['scriptLog'] = [l[l.find('Log:'):][:200] for l in d['console'] if '[Console]' in l][:40]
import re
m = [re.search(r'first frame (\d+) x (\d+)', l) for l in d['console']]
m = [x for x in m if x]
out['size'] = [int(m[0].group(1)), int(m[0].group(2))] if m else None
print(json.dumps(out, indent=1))"
