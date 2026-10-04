# NOVA 안드로이드 APK 빌드 (Gradle 없이): NDK CMake → libnova.so, aapt2 link (+ assets), zipalign, apksigner.
#   python Android/build.py [--abi x86_64,arm64-v8a] [--assets 폴더] [--out 폴더] [--debug]
# 필요: Android SDK (build-tools 36 · platforms android-34 · cmake 3.22.1 · ndk 28), Java 17+ (apksigner).
#   찾는 순서: ANDROID_HOME · JAVA_HOME → NOVA Hub 의 "Android 빌드 지원" 모듈 (<NOVA>\AndroidTools\{sdk,jdk})
#   → Android Studio 기본 위치 (%LOCALAPPDATA%\Android\Sdk, jbr). 서명 = ~/.android/debug.keystore (없으면 만든다)
import argparse, os, shutil, subprocess, sys, zipfile

HERE = os.path.dirname(os.path.abspath(__file__))


def nova_tools():
    # Hub 가 설치한 공용 도구: 엔진 폴더(<NOVA>\Editors\<버전>) 옆의 AndroidTools, 또는 기본 %LOCALAPPDATA%\NOVA\AndroidTools
    roots = [os.environ.get('NOVA_ANDROID_TOOLS'), os.path.normpath(os.path.join(HERE, '..', '..', '..', 'AndroidTools')),
             os.path.join(os.environ.get('LOCALAPPDATA', ''), 'NOVA', 'AndroidTools')]
    return [r for r in roots if r and os.path.isdir(r)]


def find_sdk():
    for sdk in [os.environ.get('ANDROID_HOME'), os.environ.get('ANDROID_SDK_ROOT')] + [os.path.join(r, 'sdk') for r in nova_tools()]:
        if sdk and os.path.isdir(os.path.join(sdk, 'build-tools')):
            return sdk
    return os.path.join(os.environ.get('LOCALAPPDATA', ''), 'Android', 'Sdk')


SDK = find_sdk()


def newest(path, prefix=''):
    items = sorted([d for d in os.listdir(path) if d.startswith(prefix)], key=lambda v: [int(x) if x.isdigit() else x for x in v.replace('-', '.').split('.')])
    return os.path.join(path, items[-1]) if items else None


def find_java():
    for home in [os.environ.get('JAVA_HOME')] + [os.path.join(r, 'jdk') for r in nova_tools()] + [r'C:\Program Files\Android\Android Studio\jbr']:
        if home and os.path.exists(os.path.join(home, 'bin', 'java.exe')):
            return home
    return None


def run(cmd, env=None, cwd=None):
    r = subprocess.run(cmd, capture_output=True, text=True, env=env, cwd=cwd, encoding='utf-8', errors='replace')
    if r.returncode:
        print('FAILED:', ' '.join(cmd))
        print(r.stdout[-6000:])
        print(r.stderr[-6000:])
        sys.exit(1)
    return r


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--abi', default='x86_64')
    ap.add_argument('--assets', default=os.path.join(HERE, 'build', 'assets'))
    ap.add_argument('--out', default=os.path.join(HERE, 'build'))
    ap.add_argument('--debug', action='store_true')
    a = ap.parse_args()

    ndk = newest(os.path.join(SDK, 'ndk'), '2')
    bt = newest(os.path.join(SDK, 'build-tools'), '3')
    jar = os.path.join(SDK, 'platforms', 'android-34', 'android.jar')
    cmake_dir = newest(os.path.join(SDK, 'cmake'))
    java = find_java()
    for name, p in [('NDK', ndk), ('build-tools', bt), ('android-34', jar), ('SDK cmake', cmake_dir), ('Java', java)]:
        if not p or not os.path.exists(p):
            sys.exit(f'missing {name} (SDK {SDK})')
    cmake = os.path.join(cmake_dir, 'bin', 'cmake.exe')
    ninja = os.path.join(cmake_dir, 'bin', 'ninja.exe')
    config = 'Debug' if a.debug else 'Release'

    libs = {}
    for abi in a.abi.split(','):
        bdir = os.path.join(a.out, 'cmake', abi + '-' + config)
        run([cmake, '-S', HERE, '-B', bdir, '-G', 'Ninja', f'-DCMAKE_MAKE_PROGRAM={ninja}',
             f'-DCMAKE_TOOLCHAIN_FILE={os.path.join(ndk, "build", "cmake", "android.toolchain.cmake")}',
             f'-DANDROID_ABI={abi}', '-DANDROID_PLATFORM=android-26', '-DANDROID_STL=c++_static', f'-DCMAKE_BUILD_TYPE={config}'])
        run([cmake, '--build', bdir])
        libs[abi] = os.path.join(bdir, 'libnova.so')
        print('built', abi, os.path.getsize(libs[abi]), 'bytes')

    os.makedirs(a.out, exist_ok=True)
    base = os.path.join(a.out, 'base.apk')
    cmd = [os.path.join(bt, 'aapt2.exe'), 'link', '-o', base, '--manifest', os.path.join(HERE, 'AndroidManifest.xml'), '-I', jar]
    if os.path.isdir(a.assets):
        cmd += ['-A', a.assets]
    run(cmd)
    with zipfile.ZipFile(base, 'a', zipfile.ZIP_DEFLATED) as z:
        for abi, so in libs.items():
            z.write(so, f'lib/{abi}/libnova.so')
    aligned = os.path.join(a.out, 'aligned.apk')
    run([os.path.join(bt, 'zipalign.exe'), '-p', '-f', '4', base, aligned])
    keystore = os.path.expanduser(r'~\.android\debug.keystore')
    env = dict(os.environ, JAVA_HOME=java, PATH=os.path.join(java, 'bin') + os.pathsep + os.environ['PATH'])
    if not os.path.exists(keystore):
        os.makedirs(os.path.dirname(keystore), exist_ok=True)
        run([os.path.join(java, 'bin', 'keytool.exe'), '-genkeypair', '-keystore', keystore, '-storepass', 'android', '-alias', 'androiddebugkey',
             '-keypass', 'android', '-keyalg', 'RSA', '-keysize', '2048', '-validity', '10000', '-dname', 'CN=Android Debug,O=Android,C=US'], env=env)
    apk = os.path.join(a.out, 'nova.apk')
    run(['cmd', '/c', os.path.join(bt, 'apksigner.bat'), 'sign', '--ks', keystore, '--ks-pass', 'pass:android', '--key-pass', 'pass:android',
         '--ks-key-alias', 'androiddebugkey', '--out', apk, aligned], env=env)
    print('apk', apk, os.path.getsize(apk), 'bytes')


if __name__ == '__main__':
    main()
