#!/usr/bin/env python3
"""Bundle an installed usr/ tree without replacing the host libc, PAM or graphics drivers."""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess

HOST = re.compile(r"^(ld-linux.*|lib(c|m|pthread|dl|rt|util|resolv|nss_.*)\.so.*|lib(EGL|GL|GLESv[12]|GLX|GLdispatch|OpenGL|drm.*|gbm|pam|pam_misc)\.so.*)$")


def output(*args):
    return subprocess.check_output(args, text=True, stderr=subprocess.STDOUT)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('stage', type=Path)
    args = parser.parse_args()
    stage = args.stage.resolve()
    libdir = stage / 'usr/lib'
    libdir.mkdir(parents=True, exist_ok=True)
    notices = stage / 'usr/share/licenses/kusanagi/bundled'
    notices.mkdir(parents=True, exist_ok=True)
    seeds = [stage / 'usr/bin/kusanagi-shell']
    # These are loaded at runtime, so ldd alone cannot discover them.
    for name, candidates in {
        'wireplumber-0.5': ['/usr/local/lib/wireplumber-0.5'],
        'pipewire-0.3': ['/usr/lib/x86_64-linux-gnu/pipewire-0.3'],
        'spa-0.2': ['/usr/lib/x86_64-linux-gnu/spa-0.2'],
    }.items():
        source = next((Path(p) for p in candidates if Path(p).is_dir()), None)
        if source is None:
            raise RuntimeError(f'Missing runtime modules: {name}')
        destination = libdir / name
        shutil.copytree(source, destination)
        seeds.extend(destination.rglob('*.so'))
    shutil.copytree('/usr/share/pipewire', stage / 'usr/share/pipewire')
    shutil.copytree('/usr/local/share/kusanagi-bundled-licenses', notices / 'upstream')
    shutil.copytree('/usr/share/common-licenses', notices / 'common-licenses')

    packages = set()
    libraries = {}
    for binary in seeds:
        for line in output('ldd', str(binary)).splitlines():
            if 'not found' in line:
                raise RuntimeError(f'{binary}: {line}')
            match = re.match(r'\s*(\S+) => (/\S+)', line)
            if not match:
                continue
            name, filename = match.groups()
            if HOST.match(name):
                continue
            source = Path(filename)
            if name in libraries:
                continue
            shutil.copy2(source, libdir / name, follow_symlinks=True)
            libraries[name] = filename
            for candidate in dict.fromkeys([str(source), str(source.resolve()), str(source).removeprefix('/usr')]):
                try:
                    owner = output('dpkg-query', '-S', candidate).split(': ', 1)[0]
                    packages.add(owner)
                    break
                except subprocess.CalledProcessError:
                    pass

    manifest = []
    for package in sorted(packages):
        fields = output('dpkg-query', '-W', '-f=${binary:Package}\t${Version}\t${source:Package}\t${source:Version}', package).split('\t')
        manifest.append(dict(zip(['package', 'version', 'source', 'source_version'], fields)))
        copyright_file = Path('/usr/share/doc') / package.split(':')[0] / 'copyright'
        if copyright_file.exists():
            shutil.copy2(copyright_file, notices / (package.replace(':', '_') + '.copyright'))
    (notices / 'packages.json').write_text(json.dumps(manifest, indent=2) + '\n')
    (notices / 'libraries.json').write_text(json.dumps(libraries, indent=2) + '\n')

    executable = stage / 'usr/libexec/kusanagi-shell'
    executable.parent.mkdir(parents=True, exist_ok=True)
    seeds[0].rename(executable)
    seeds[0] = executable
    # RUNPATH applies to these ELF files, not to applications launched by the shell.
    for binary in [*seeds, *(libdir / name for name in libraries)]:
        relative = os.path.relpath(libdir, binary.parent)
        rpath = '$ORIGIN' if relative == '.' else '$ORIGIN/' + relative
        subprocess.run(['patchelf', '--set-rpath', rpath, str(binary)], check=True)
    source_dir = Path(__file__).resolve().parent
    for source, target in [('launch-shell', stage / 'usr/bin/kusanagi-shell'),
                           ('kusanagi', stage / 'kusanagi'), ('install', stage / 'install')]:
        shutil.copy2(source_dir / source, target)
        target.chmod(0o755)
    print(f'Bundled {len(libraries)} libraries; host libc, PAM and graphics libraries retained.')


if __name__ == '__main__':
    main()
