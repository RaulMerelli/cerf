import re
import os

ROOT = os.path.dirname(os.path.abspath(__file__))
VERSION_H = os.path.join(ROOT, 'cerf', 'version.h')
SOURCE    = os.path.join(ROOT, 'README_SOURCE.md')
OUTPUT    = os.path.join(ROOT, 'README.md')
CHANGELOG = os.path.join(ROOT, 'docs', 'changelog.html')
CHANGELOG_LINK = 'docs/changelog.html'
CHANGELOG_RECENT = 3

ICONS = {
    'i_display':   '<img src="launcher/assets/icons/display.png" width="16" height="16" title="Graphics" alt="Graphics"/>',
    'i_speaker':   '<img src="launcher/assets/icons/speaker.png" width="16" height="16" title="Sound" alt="Sound"/>',
    'i_stylus':    '<img src="launcher/assets/icons/stylus.png" width="16" height="16" title="Touch" alt="Touch"/>',
    'i_keyboard':  '<img src="launcher/assets/icons/keyboard.png" width="16" height="16" title="Keyboard" alt="Keyboard"/>',
    'i_internet':  '<img src="launcher/assets/icons/internet.png" width="16" height="16" title="Network Emulation" alt="Network Emulation"/>',
    'i_pda':       '<img src="launcher/assets/icons/pda.png" width="16" height="16" title="PDA" alt="PDA"/>',
    'i_chip':      '<img src="launcher/assets/icons/chip.png" width="16" height="16" title="Chip" alt="Chip"/>',
    'i_os_ce':     '<img src="launcher/assets/icons/os_ce.png" width="16" height="16" title="Windows CE" alt="Windows CE"/>',
    'i_os_old_ce': '<img src="launcher/assets/icons/os_old_ce.png" width="16" height="16" title="Windows CE (Classic)" alt="Windows CE (Classic)"/>',
    'i_os_ppc2000':'<img src="launcher/assets/icons/os_ppc2000.png" width="16" height="16" title="Pocket PC 2000" alt="Pocket PC 2000"/>',
    'i_os_ppc2002':'<img src="launcher/assets/icons/os_ppc2002.png" width="16" height="16" title="PPC2002+ Icon" alt="PPC2002+ Icon"/>',
    'i_os_wm6':    '<img src="launcher/assets/icons/os_wm6.png" width="16" height="16" title="Windows Mobile 6+" alt="Windows Mobile 6+"/>',
    'i_os_zune':   '<img src="launcher/assets/icons/os_zune.png" width="16" height="16" title="Zune OS" alt="Zune OS"/>',
    'i_os_zune_hd':'<img src="launcher/assets/icons/os_zune_hd.png" width="16" height="16" title="Zune HD OS" alt="Zune HD OS"/>',
}


def parse_version():
    with open(VERSION_H, 'r') as f:
        text = f.read()
    major = int(re.search(r'#define CERF_VERSION_MAJOR\s+(\d+)', text).group(1))
    minor = int(re.search(r'#define CERF_VERSION_MINOR\s+(\d+)', text).group(1))
    patch = int(re.search(r'#define CERF_VERSION_PATCH\s+(\d+)', text).group(1))
    return f'{major}.{minor}' if patch == 0 else f'{major}.{minor}.{patch}'


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
        lines.append('      <td colspan="2"><b>Previous versions</b> — '
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
    for key, value in ICONS.items():
        content = content.replace('{' + key + '}', value)

    with open(OUTPUT, 'w', encoding='utf-8') as f:
        f.write(content)

    print(f'README.md compiled (v{version})')


if __name__ == '__main__':
    main()
