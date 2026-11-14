"""Find silent shadows in a block-merging INI such as ParticleSystemReforged.ini.

usage: python ini_shadow_check.py <file.ini> [--from <text>] [--kind ParticleSystem]

A later block of the same name merges key by key over an earlier one, so a key set in a block is
lost when a block further down sets it again with another value. This lists every such key: the
block and line that set it, and the block and line that overrides it. --from limits the check to
blocks that start after the first line beginning with <text> (for example a section's opening
comment), so earlier passes that were meant to be overridden stay quiet. Exit code is the number of
shadowed keys, capped at 255.
"""
import sys


def blocks(path, kind):
    found = []
    cur = None
    with open(path, encoding='latin-1') as f:
        lines = f.read().splitlines()
    for i, line in enumerate(lines):
        s = line.split(';', 1)[0].strip()
        if not s:
            continue
        if s.startswith(kind + ' ') and cur is None:
            cur = (s.split()[1], i + 1, {})
            continue
        if s == 'End' and cur is not None:
            found.append(cur)
            cur = None
            continue
        if cur is not None and '=' in s:
            key, value = s.split('=', 1)
            cur[2][key.strip()] = ' '.join(value.split())
    return lines, found


def main(argv):
    if len(argv) < 2:
        print(__doc__.strip().splitlines()[2])
        return 2
    path = argv[1]
    start_text = None
    kind = 'ParticleSystem'
    rest = argv[2:]
    while rest:
        if rest[0] == '--from' and len(rest) > 1:
            start_text, rest = rest[1], rest[2:]
        elif rest[0] == '--kind' and len(rest) > 1:
            kind, rest = rest[1], rest[2:]
        else:
            print('unknown argument: %s' % rest[0])
            return 2
    lines, found = blocks(path, kind)
    start = 0
    if start_text is not None:
        hits = [i + 1 for i, line in enumerate(lines) if line.startswith(start_text)]
        if not hits:
            print('no line starts with %r' % start_text)
            return 2
        start = hits[0]
    by_name = {}
    for name, line, keys in found:
        by_name.setdefault(name, []).append((line, keys))
    count = 0
    for name, line, keys in found:
        if line < start:
            continue
        for later_line, later_keys in by_name[name]:
            if later_line <= line:
                continue
            for key, value in keys.items():
                if key in later_keys and later_keys[key] != value:
                    print('%s line %d: %s = %s  overridden at line %d by %s'
                          % (name, line, key, value, later_line, later_keys[key]))
                    count += 1
    print('%d shadowed key(s)' % count)
    return min(count, 255)


if __name__ == '__main__':
    sys.exit(main(sys.argv))
