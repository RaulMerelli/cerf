"""Builds the CERF website into build/site.

The board table, the changelog and the version come from the same sources the
launcher and README.md use (launcher/supported_devices.py, docs/changelog.html,
cerf/version.h), so the site cannot drift from them. Pages are staged into
build/site_src first: generated pages and copied assets never touch the repo.
"""

import os
import re
import shutil
import subprocess
import sys

ROOT      = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SITE_SRC  = os.path.join(ROOT, 'docs', 'website')
STAGE     = os.path.join(ROOT, 'build', 'site_src')
OUT       = os.path.join(ROOT, 'build', 'site')
CHANGELOG = os.path.join(ROOT, 'docs', 'changelog.html')
ICONS_SRC = os.path.join(ROOT, 'launcher', 'assets', 'icons')

IMAGES = [
    ('gweslab.png',                        'gweslab.png'),
    (os.path.join('docs', 'cerf_youtube.png'), 'cerf_youtube.png'),
    (os.path.join('docs', 'launcher.png'),     'launcher.png'),
    (os.path.join('launcher', 'assets', 'GaBanner.png'), 'GaBanner.png'),
]

sys.path.insert(0, ROOT)
import compile_readme


def stage():
    if os.path.isdir(STAGE):
        shutil.rmtree(STAGE)
    shutil.copytree(SITE_SRC, STAGE)

    content = os.path.join(STAGE, 'content')
    icons = os.path.join(content, 'assets', 'icons')
    shutil.copytree(ICONS_SRC, icons,
                    ignore=shutil.ignore_patterns('*.md'))

    img_dir = os.path.join(content, 'assets', 'img')
    os.makedirs(img_dir, exist_ok=True)
    for src, name in IMAGES:
        shutil.copyfile(os.path.join(ROOT, src), os.path.join(img_dir, name))

    return content


def boards_page():
    table = compile_readme.build_supported_devices()
    table = table.replace('launcher/assets/icons', 'assets/icons')
    return ('# Supported boards\n\n'
            'A ROM boots only if its board is implemented in CERF. A matching SoC is not\n'
            'enough - the same chip on another board has a different memory map, display\n'
            'controller and wiring.\n\n'
            '<div class="cerf-boards" markdown>\n\n'
            f'{table}\n\n'
            '</div>\n')


def changelog_page():
    with open(CHANGELOG, 'r', encoding='utf-8') as f:
        html = f.read()
    tbody = re.search(r'<tbody>(.*?)</tbody>', html, re.DOTALL).group(1)
    rows = re.findall(r'<tr>.*?</tr>', tbody, re.DOTALL)

    lines = ['# Changelog', '', '<table>', '  <thead>', '    <tr>',
             '      <th>Version</th>', '      <th>Release date</th>',
             '      <th>Changes</th>', '    </tr>', '  </thead>', '  <tbody>']
    for row in rows:
        lines.append('    ' + row.strip().replace('\n', '\n    '))
    lines += ['  </tbody>', '</table>', '']
    return '\n'.join(lines)


def substitute_version(content, version):
    for dirpath, _, names in os.walk(content):
        for name in names:
            if not name.endswith('.md'):
                continue
            path = os.path.join(dirpath, name)
            with open(path, 'r', encoding='utf-8') as f:
                text = f.read()
            if '{version}' not in text:
                continue
            with open(path, 'w', encoding='utf-8') as f:
                f.write(text.replace('{version}', version))


def main():
    version = compile_readme.parse_version()
    content = stage()

    with open(os.path.join(content, 'boards.md'), 'w', encoding='utf-8') as f:
        f.write(boards_page())
    with open(os.path.join(content, 'changelog.md'), 'w', encoding='utf-8') as f:
        f.write(changelog_page())

    substitute_version(content, version)

    subprocess.check_call([sys.executable, '-m', 'mkdocs', 'build',
                           '--strict',
                           '-f', os.path.join(STAGE, 'mkdocs.yml'),
                           '-d', OUT])
    print(f'site built into {OUT} (v{version})')


if __name__ == '__main__':
    main()
