"""웹 (.NET 웹어셈블리) 의 네이티브 호출 모양 만들기.

.NET 웹어셈블리 런타임은 네이티브 함수를 부를 때 빌드 때 본 모양 (인자 · 반환 종류) 의 다리만 갖고 있다. 그 모양은 DllImport 에서만 모으고,
엔진 API 표 (NativeApi.cs 의 delegate* unmanaged — 함수 포인터로 부름) 는 보지 않는다 → 표에만 있는 모양을 부르면 런타임이 멈춘다
("aot-runtime-wasm.c ... CANNOT HANDLE INTERP ICALL SIG").
그래서 ScriptCore 의 모든 delegate* unmanaged<…> 모양마다 쓰지 않는 DllImport 하나 (NovaWebSignatures.cs) 와
그 빈 C 함수 (host-modules/NovaWeb.c) 를 만든다 — 모양만 같으면 다리를 같이 쓴다.

  python Tools/web/gen_native_signatures.py <ScriptCore 폴더> <NovaWebSignatures.cs> <NovaWeb.c>
"""
import pathlib
import re
import sys

C_TYPES = {'int': 'int', 'uint': 'unsigned int', 'long': 'long long', 'ulong': 'unsigned long long', 'float': 'float',
           'double': 'double', 'void': 'void', 'IntPtr': 'void*', 'nint': 'void*', 'byte': 'unsigned char', 'bool': 'int',
           'short': 'short', 'ushort': 'unsigned short', 'sbyte': 'signed char'}
CS_TYPES = {'IntPtr': 'void*', 'nint': 'void*', 'bool': 'int'}


def norm(t: str) -> str:
    t = t.strip()
    return 'void*' if t.endswith('*') else t


def main() -> int:
    core, out_cs, out_c = map(pathlib.Path, sys.argv[1:4])
    sigs = set()
    for f in core.rglob('*.cs'):
        if any(p in ('bin', 'obj') for p in f.parts):
            continue
        for m in re.finditer(r'delegate\*\s*unmanaged(?:\[[^\]]*\])?<([^>]*)>', f.read_text(encoding='utf-8', errors='ignore')):
            parts = [norm(p) for p in m.group(1).split(',')]
            if all(p == 'void*' or p in C_TYPES for p in parts):
                sigs.add(tuple(parts))
    sigs = sorted(sigs)
    cs = ['// 만든 파일 (Tools/web/gen_native_signatures.py): 엔진 API 표의 호출 모양마다 쓰지 않는 DllImport 하나 — .NET 이 그 모양의 다리를 만들게',
          'using System.Runtime.InteropServices;', '', 'internal static unsafe class NovaWebSignatures', '{']
    c = ['/* 만든 파일 (Tools/web/gen_native_signatures.py): NovaWebSignatures.cs 의 빈 함수 — 모듈 이름 (NovaWeb) 도 이 파일 */']
    for i, sig in enumerate(sigs):
        *args, ret = sig
        cs_args = ', '.join(f'{CS_TYPES.get(a, a)} a{n}' for n, a in enumerate(args))
        cs.append(f'    [DllImport("NovaWeb")] internal static extern {CS_TYPES.get(ret, ret)} nova_sig_{i}({cs_args});')
        c_args = ', '.join(f'{"void*" if a == "void*" else C_TYPES[a]} a{n}' for n, a in enumerate(args)) or 'void'
        c_ret = 'void*' if ret == 'void*' else C_TYPES[ret]
        body = '' if c_ret == 'void' else ' return 0;'
        c.append(f'{c_ret} nova_sig_{i}({c_args}) {{{body} }}')
    cs.append('}')
    out_cs.write_text('\n'.join(cs) + '\n', encoding='utf-8')
    out_c.write_text('\n'.join(c) + '\n', encoding='utf-8')
    print(f'{len(sigs)} native call shapes')
    return 0


if __name__ == '__main__':
    sys.exit(main())
