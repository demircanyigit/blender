#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Blender Authors
# SPDX-License-Identifier: GPL-2.0-or-later
"""Fetch verified header-only dependencies for the optional CGAL Decimate backend."""

import argparse
import hashlib
from pathlib import Path
import tarfile
import urllib.request

SOURCES = (
    ('cgal', 'https://github.com/CGAL/cgal/releases/download/v6.2.1/CGAL-6.2.1-library.tar.xz',
     '8e7ce1e30e8ffcb3e69d153c3a561bb1f38bbb4becd8e6544cbaa0fb2889af62', 'cgal.tar.xz'),
    ('boost', 'https://archives.boost.io/release/1.88.0/source/boost_1_88_0.tar.gz',
     '3621533e820dcab1e8012afd583c0c73cf0f77694952b81352bf38c1488f9cb4', 'boost.tar.gz'),
)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archives', type=Path, help='Use archives already downloaded into this folder')
    parser.add_argument('--destination', type=Path)
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[2]
    destination = (args.destination or repo / 'lib' / 'decimation').resolve()
    destination.mkdir(parents=True, exist_ok=True)
    for name, url, expected, filename in SOURCES:
        archive = (args.archives or destination / 'archives') / filename
        archive.parent.mkdir(parents=True, exist_ok=True)
        if not archive.exists():
            print('Downloading', name, flush=True)
            request = urllib.request.Request(url, headers={'User-Agent': 'Blender-Decimate-dependencies'})
            with urllib.request.urlopen(request, timeout=120) as response, archive.open('wb') as output:
                while chunk := response.read(1024 * 1024):
                    output.write(chunk)
        if hashlib.sha256(archive.read_bytes()).hexdigest() != expected:
            raise RuntimeError('SHA256 mismatch: ' + str(archive))
        target = destination / name
        with tarfile.open(archive) as tar:
            for member in tar:
                relative = Path(*Path(member.name).parts[1:])
                if not member.isfile() or not relative.parts or '..' in relative.parts:
                    continue
                keep = relative.parts[0] in (('include', 'licenses') if name == 'cgal' else ('boost',))
                keep |= relative.name.startswith(('LICENSE', 'COPYING'))
                if not keep:
                    continue
                file = (target / relative).resolve()
                if not file.is_relative_to(target.resolve()):
                    raise RuntimeError('Unsafe archive member: ' + member.name)
                file.parent.mkdir(parents=True, exist_ok=True)
                with tar.extractfile(member) as source, file.open('wb') as output:
                    output.write(source.read())
        (target / 'SOURCE.txt').write_text(url + '\nSHA256: ' + expected + '\n', encoding='utf-8')
        print('Ready:', target, flush=True)


if __name__ == '__main__':
    main()
