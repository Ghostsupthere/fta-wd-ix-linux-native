#!/usr/bin/env python3
"""Turn Logs/samples.txt (WXL_SAMPLE_PROFILE, see Engine/Source/Base/Profiler.cpp) into a per-function report.

Usage: symbolize-samples.py SAMPLES.txt [--zone NAME] [--top N] [--depth D] [--inline]
                            [--module MAPPED_PATH=SYMBOL_FILE]

Samples carry the profiler zone the render thread was in. --zone keeps only that zone (for example "UI render");
without it every sample counts. The report shows the innermost function (self time) and the first D functions
after sampler frames are removed (inclusive time). --inline expands inline frames when debug information is present.
--module reads symbols from a saved build while retaining the address ranges recorded in the sample.
"""
import argparse
import collections
import re
import subprocess
import sys


def load(path):
    modules, samples = [], []
    for line in open(path):
        parts = line.split()
        if not parts:
            continue
        if parts[0] == '#':
            continue  # zone legend (see zone_names) and the sample count
        elif parts[0] == 'M':
            modules.append((int(parts[1], 16), int(parts[2], 16), int(parts[3], 16), ' '.join(parts[4:])))
        elif parts[0] == 'S':
            samples.append((int(parts[1]), [int(p, 16) for p in parts[2:]]))
    return modules, samples


def zone_names(path):
    # Zone names contain spaces ("UI render"), so read the legend line by hand.
    for line in open(path):
        if line.startswith('# zones'):
            names, current = {}, None
            for token in line.split()[2:]:
                if '=' in token and token.split('=')[0].isdigit():
                    current = int(token.split('=')[0])
                    names[current] = token.split('=', 1)[1]
                elif current is not None:
                    names[current] += ' ' + token
            return names
    return {}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('samples')
    parser.add_argument('--zone')
    parser.add_argument('--top', type=int, default=30)
    parser.add_argument('--depth', type=int, default=8)
    parser.add_argument('--inline', action='store_true')
    parser.add_argument('--module', action='append', default=[], metavar='MAPPED_PATH=SYMBOL_FILE')
    args = parser.parse_args()
    if args.depth < 1:
        parser.error('--depth must be positive')
    modules, samples = load(args.samples)
    symbol_files = {}
    mapped_paths = {path for _, _, _, path in modules}
    for mapping in args.module:
        path, separator, symbols = mapping.partition('=')
        if not separator or not symbols or path not in mapped_paths:
            parser.error(f'expected a recorded module path and symbol file, got {mapping!r}')
        symbol_files[path] = symbols
    names = zone_names(args.samples)
    by_zone = collections.Counter(names.get(z, 'outside zones' if z < 0 else str(z)) for z, _ in samples)
    print('samples per zone:', dict(by_zone.most_common()))
    if args.zone:
        samples = [(z, f) for z, f in samples if names.get(z, 'outside zones' if z < 0 else str(z)) == args.zone]
    print(len(samples), 'samples analysed')

    # A module's load base is the start of its mapping at file offset 0; addr2line wants addresses relative to it.
    bases = {}
    for start, end, offset, path in modules:
        if offset == 0 and (path not in bases or start < bases[path]):
            bases[path] = start

    def locate(address):
        for start, end, offset, path in modules:
            if start <= address < end and path in bases:
                return path, address - bases[path]
        return None, address

    wanted = collections.defaultdict(set)
    for _, frames in samples:
        for address in frames:
            path, offset = locate(address)
            if path:
                wanted[path].add(offset - 1 if offset else offset)  # return addresses point past the call
    resolved = {}
    for path, offsets in wanted.items():
        command = ['addr2line', '-a', '-f', '-C']
        if args.inline:
            command.append('-i')
        command.extend(['-e', symbol_files.get(path, path)])
        try:
            out = subprocess.run(command, input=''.join(f'{o:x}\n' for o in sorted(offsets)),
                                 capture_output=True, text=True, check=True).stdout.splitlines()
        except (OSError, subprocess.CalledProcessError) as error:
            if path in symbol_files:
                parser.error(f'could not read symbols from {symbol_files[path]!r}: {error}')
            continue
        frames = iter(out)
        for line in frames:
            if re.fullmatch(r'0x[0-9a-fA-F]+', line):
                offset = int(line, 16)
                resolved[path, offset] = []
            else:
                next(frames, None)  # source position
                if line != '??':
                    resolved[path, offset].append(line)

    def functions(address):
        path, offset = locate(address)
        if path is None:
            return [f'{address:x}']
        return resolved.get((path, offset - 1 if offset else offset)) or [f'{path.rsplit("/", 1)[-1]}+0x{offset:x}']

    skip = ('__sigaction', '__restore_rt', 'backtrace', '_Unwind', '__GI_')
    self_time, inclusive = collections.Counter(), collections.Counter()
    for _, frames in samples:
        stack = [function for address in frames for function in functions(address)]
        stack = [f for f in stack if not f.startswith(skip) and 'OnSample' not in f]
        if not stack:
            continue
        self_time[stack[0]] += 1
        for function in dict.fromkeys(stack[:args.depth]):
            inclusive[function] += 1
    total = max(len(samples), 1)
    print('\nself time (innermost function):')
    for function, count in self_time.most_common(args.top):
        print('%6.1f%%  %s' % (100.0 * count / total, function))
    print('\nwith the call stack inclusive (any of the top frames):')
    for function, count in inclusive.most_common(args.top):
        print('%6.1f%%  %s' % (100.0 * count / total, function))


if __name__ == '__main__':
    sys.exit(main())
