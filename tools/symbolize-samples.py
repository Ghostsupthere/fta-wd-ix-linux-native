#!/usr/bin/env python3
"""Turn Logs/samples.txt (WXL_SAMPLE_PROFILE, see Engine/Source/Base/Profiler.cpp) into a per-function report.

Usage: symbolize-samples.py SAMPLES.txt [--zone NAME] [--top N] [--depth D]

Samples carry the profiler zone the render thread was in. --zone keeps only that zone (for example "UI render");
without it every sample counts. Each sample's frames are listed innermost first: the report shows the functions
at the top of the stack (self time) and, with --depth > 1, the callers above them (inclusive time).
Addresses are resolved with addr2line against the executable and shared objects named in the file's module map.
"""
import argparse
import collections
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
    return None, modules, samples


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
    parser.add_argument('--depth', type=int, default=1)
    args = parser.parse_args()
    _, modules, samples = load(args.samples)
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
        for address in frames[:max(args.depth, 1) + 2]:
            path, offset = locate(address)
            if path:
                wanted[path].add(offset - 1 if offset else offset)  # return addresses point past the call
    resolved = {}
    for path, offsets in wanted.items():
        ordered = sorted(offsets)
        try:
            out = subprocess.run(['addr2line', '-f', '-C', '-e', path] + [hex(o) for o in ordered],
                                 capture_output=True, text=True, check=True).stdout.splitlines()
        except (OSError, subprocess.CalledProcessError):
            continue
        for index, offset in enumerate(ordered):
            function = out[index * 2] if index * 2 < len(out) else '??'
            resolved[(path, offset)] = function if function != '??' else '%s+0x%x' % (path.rsplit('/', 1)[-1], offset)

    def name(address):
        path, offset = locate(address)
        return resolved.get((path, offset - 1 if offset else offset), '%x' % address)

    skip = ('__sigaction', '__restore_rt', 'backtrace', '_Unwind', '__GI_')
    self_time, inclusive = collections.Counter(), collections.Counter()
    for _, frames in samples:
        stack = [name(a) for a in frames]
        stack = [f for f in stack if not f.startswith(skip) and 'OnSample' not in f]
        if not stack:
            continue
        self_time[stack[0]] += 1
        for function in set(stack[:max(args.depth, 8)]):
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
