import re
import os
import sys

ROOT = os.path.dirname(os.path.abspath(__file__))
VERSION_H = os.path.join(ROOT, 'cerf', 'version.h')
SOURCE    = os.path.join(ROOT, 'README_SOURCE.md')
OUTPUT    = os.path.join(ROOT, 'README.md')
CHANGELOG = os.path.join(ROOT, 'docs', 'changelog.html')
CHANGELOG_LINK = 'docs/changelog.html'
CHANGELOG_RECENT = 3
ICONS_DIR = 'launcher/assets/icons'

sys.path.insert(0, os.path.join(ROOT, 'launcher'))
from supported_devices import BOARDS_INFORMATION, FEATURE_SPECS, board_sort_key


def parse_version():
    with open(VERSION_H, 'r') as f:
        text = f.read()
    major = int(re.search(r'#define CERF_VERSION_MAJOR\s+(\d+)', text).group(1))
    minor = int(re.search(r'#define CERF_VERSION_MINOR\s+(\d+)', text).group(1))
    patch = int(re.search(r'#define CERF_VERSION_PATCH\s+(\d+)', text).group(1))
    return f'{major}.{minor}' if patch == 0 else f'{major}.{minor}.{patch}'


def icon_img(filename, title):
    return (f'<img src="{ICONS_DIR}/{filename}" width="16" height="16" '
            f'title="{title}" alt="{title}"/>')


def features_cell(features):
    icons = [icon_img(filename, label)
             for key, filename, label in FEATURE_SPECS if features.get(key)]
    return ' '.join(icons) if icons else '&mdash;'


def build_supported_devices():
    boards = sorted((b for b in BOARDS_INFORMATION if b.get('supported')),
                    key=lambda b: board_sort_key(b['name']))

    # All boards on the same SoC share one rowspan SoC cell. Grouping is by SoC
    # globally (not just consecutive boards): the alphabetical board order can
    # interleave SoCs (e.g. Jornada 820 / SA-1100 sorts between Jornada 720 and
    # iPAQ / SA-1110), which would otherwise split one SoC across two cells. SoC
    # groups appear in first-board order; boards stay in board order within each.
    groups = []
    soc_index = {}
    for board in boards:
        soc = board['soc']
        if soc in soc_index:
            soc_index[soc].append(board)
        else:
            members = [board]
            soc_index[soc] = members
            groups.append((soc, members))

    lines = [
        '<table>',
        '  <thead>',
        '    <tr>',
        '      <th>SoC</th>',
        '      <th>Board / OS</th>',
        '      <th>Features</th>',
        '    </tr>',
        '  </thead>',
        '  <tbody>',
    ]
    for soc, group in groups:
        for index, board in enumerate(group):
            lines.append('    <tr>')
            if index == 0:
                rowspan = f' rowspan="{len(group)}"' if len(group) > 1 else ''
                lines.append(f'      <td{rowspan} align="center">'
                             f'<b>{icon_img("chip.png", "Chip")} {soc.family} ({soc.cpu})</b>'
                             f'<br/><sub>{soc.arch}</sub></td>')
            cell = [f'{icon_img("pda.png", "PDA")} <b>{board["name"]}</b>']
            cell += [f'{icon_img(guest_os.icon, guest_os.name)} {guest_os.name}'
                     for guest_os in board['operating_systems']]
            lines.append('      <td>')
            lines.append('        ' + '<br/>\n        '.join(cell))
            lines.append('      </td>')
            lines.append(f'      <td>{features_cell(board.get("features", {}))}</td>')
            lines.append('    </tr>')
    lines.append('  </tbody>')
    lines.append('</table>')
    return '\n'.join(lines)


def build_changelog():
    with open(CHANGELOG, 'r', encoding='utf-8') as f:
        html = f.read()

    tbody = re.search(r'<tbody>(.*?)</tbody>', html, re.DOTALL).group(1)
    rows = re.findall(r'<tr>.*?</tr>', tbody, re.DOTALL)
    recent = rows[:CHANGELOG_RECENT]

    lines = [
        '<table>',
        '  <thead>',
        '    <tr>',
        '      <th>CERF Version</th>',
        '      <th>Changes</th>',
        '    </tr>',
        '  </thead>',
        '  <tbody>',
    ]
    for row in recent:
        lines.append('    ' + row.strip().replace('\n', '\n    '))
    if len(rows) > CHANGELOG_RECENT:
        lines.append('    <tr>')
        lines.append('      <td colspan="2"><b>Previous versions</b> - '
                     f'see the <a href="{CHANGELOG_LINK}">full changelog</a>.</td>')
        lines.append('    </tr>')
    lines.append('  </tbody>')
    lines.append('</table>')
    return '\n'.join(lines)


def main():
    with open(SOURCE, 'r', encoding='utf-8') as f:
        content = f.read()

    version = parse_version()
    content = content.replace('{version}', version)
    content = content.replace('{changelog}', build_changelog())
    content = content.replace('{supported_devices}', build_supported_devices())

    with open(OUTPUT, 'w', encoding='utf-8') as f:
        f.write(content)

    print(f'README.md compiled (v{version})')


if __name__ == '__main__':
    main()
