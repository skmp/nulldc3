#!/usr/bin/env python3
"""Speed of the emulator on a guest program, in percent of real time.

    perf.py --emulator <binary> --program <elf> [--renderer none|gl] [--engines jit,interp]
            [--seconds 60] [--runs 5] [--json <file>]

Each run is one process: the program runs for --seconds emulated seconds (config:Exit.AfterSeconds). Two
figures come out of it, both in percent of real time:

  speed       what the emulator prints when it ends: emulated time over wall time, from the first vblank on.
              It is what a person sees, and it falls with every moment the run waits for a core.
  CPU speed   emulated time over the CPU time of the process (user + system, every thread, from its start to
              its end). It is what the run costs: waiting for a core is not in it. A busy machine still lowers
              it, by less.

The runs of an engine are made one after the other and the median of each figure is reported, next to the lowest
and the highest and the load of the machine in front of every run. The result goes to stdout as a table and,
with --json, to a file as JSON. Not a test: nothing here passes or fails on a number.

  --renderer none   headless, config:pvr.backend=none. Any build of the emulator.
  --renderer gl     a window and the gles renderer, which only a -DHAS_TA_LLE=OFF build has. Needs a display
                    and a GL context of a GPU: "Accelerated: yes" from glxinfo -B, as the environment is or,
                    failing that, with GALLIUM_DRIVER=d3d12 (WSL2). The GL renderer string the emulator itself
                    prints is recorded, and a run on a software rasteriser (llvmpipe) is refused: its number
                    is not the speed of the hardware renderer. --allow-software-gl measures it anyway and labels
                    the result gl-software.

The reference software renderers (refsw2, refmister) are not offered: they make the run render-bound.
"""

import argparse
import datetime
import json
import os
import platform
import re
import resource
import shutil
import statistics
import subprocess
import sys
import tempfile
import time

# 1/1/2000: every run reads the same clock
RTC_PIN = 946684800

SOFTWARE_GL = re.compile(r'llvmpipe|softpipe|swrast|swiftshader|software', re.I)


def glxinfo(environment):
    """(accelerated, renderer string) of the GL context glxinfo gets in that environment, None without one."""
    try:
        out = subprocess.run(['glxinfo', '-B'], env=environment, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                             timeout=60).stdout.decode('utf-8', 'replace')
    except (OSError, subprocess.TimeoutExpired):
        return None
    accelerated = re.search(r'Accelerated:\s*(\w+)', out)
    renderer = re.search(r'OpenGL renderer string:\s*(.*)', out)
    if not accelerated or not renderer:
        return None
    return accelerated.group(1).lower() == 'yes', renderer.group(1).strip()


def gl_environment(allow_software):
    """The environment for the runs with a GL renderer and what glxinfo says about it."""
    environment = dict(os.environ)
    info = glxinfo(environment)

    if (info is None or not info[0]) and 'GALLIUM_DRIVER' not in environment:
        # WSL2: the default context is llvmpipe, the D3D12 driver of Mesa is the GPU
        trial = dict(environment, GALLIUM_DRIVER='d3d12')
        trial_info = glxinfo(trial)
        if trial_info is not None and trial_info[0]:
            environment, info = trial, trial_info

    if info is None:
        sys.exit('perf: no GL context (glxinfo -B gives none, is DISPLAY set?)')
    if not info[0] and not allow_software:
        sys.exit('perf: no GL context of a GPU: glxinfo -B says "Accelerated: no" (%s). Not measured: a number '
                 'from a software rasteriser is not the speed of the hardware renderer (--allow-software-gl '
                 'measures it and labels it gl-software)' % info[1])
    return environment, info


def machine():
    model = ''
    try:
        with open('/proc/cpuinfo') as f:
            for line in f:
                if line.startswith('model name'):
                    model = line.split(':', 1)[1].strip()
                    break
    except OSError:
        pass
    return {'cpu': model or platform.processor(), 'cores': os.cpu_count(), 'kernel': platform.release(),
            'system': platform.system()}


def run_once(options, engine, environment, directory):
    # the directory of the run's files (--portable)
    portable = os.path.join(directory, 'portable')
    os.makedirs(portable)

    keys = ['audio:backend=none', 'config:Dreamcast.RTC=%d' % RTC_PIN,
            'config:Dynarec.Enabled=' + ('yes' if engine == 'jit' else 'no'),
            'config:Exit.AfterSeconds=%g' % options.seconds]
    if options.renderer == 'none':
        keys += ['config:Headless=yes', 'config:pvr.backend=none']
        base = {}
    else:
        keys += ['config:pvr.backend=gles']
        base = dict(environment)

    args = [options.emulator, '--portable', portable, '--config']
    for i, key in enumerate(keys):
        if i:
            args.append(',')
        args.append(key)
    args.append(options.program)

    if options.renderer != 'none':
        # the shader cache of the GL driver starts empty in every run, as it did when each run had a HOME of its own
        base['XDG_CACHE_HOME'] = os.path.join(directory, 'cache')

    def limit_core():
        resource.setrlimit(resource.RLIMIT_CORE, (1, resource.getrlimit(resource.RLIMIT_CORE)[1]))

    load = os.getloadavg()[0]
    watchdog = max(300, options.seconds * 60)   # down to 1/60 of real time before the run is given up
    # the runs are made one at a time: what the children of this process used in between is the run
    before = resource.getrusage(resource.RUSAGE_CHILDREN)
    try:
        out = subprocess.run(args, cwd=directory, env=base, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                             stderr=subprocess.STDOUT, timeout=watchdog, preexec_fn=limit_core)
    except subprocess.TimeoutExpired:
        sys.exit('perf: the run did not end within %d s: %s' % (watchdog, ' '.join(args)))
    after = resource.getrusage(resource.RUSAGE_CHILDREN)
    cpu = (after.ru_utime - before.ru_utime) + (after.ru_stime - before.ru_stime)
    log = out.stdout.decode('utf-8', 'replace')

    line = re.search(r'^nulldc-exit: (.*)$', log, re.M)
    fields = dict(re.findall(r'(\w+)=(\S+)', line.group(1))) if line else {}
    if out.returncode != 0 or fields.get('reason') != 'seconds':
        sys.exit('perf: the run failed (exit code %s): %s\n%s' % (out.returncode, ' '.join(args),
                                                             '\n'.join(log.splitlines()[-25:])))

    used = re.search(r'^RendIF: Using renderer: (\S+)', log, re.M)
    gl = re.search(r'^OpenGL renderer: (.*)$', log, re.M)
    emu = float(fields['emu_s'])
    return {'speed_pct': float(fields['speed_pct']), 'wall_s': float(fields['wall_s']),
            'cpu_s': cpu, 'speed_cpu_pct': emu / cpu * 100 if cpu > 0 else 0.0,
            'emu_s': emu, 'vblanks': int(fields['vblanks']), 'load_1m': load,
            'renderer': used.group(1) if used else None, 'gl_renderer': gl.group(1).strip() if gl else None}


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--emulator', required=True)
    parser.add_argument('--program', required=True, help='the guest program, an .elf that boots under reios')
    parser.add_argument('--renderer', choices=('none', 'gl'), default='none')
    parser.add_argument('--engines', default='jit,interp', help='jit, interp or both (jit,interp)')
    parser.add_argument('--seconds', type=float, default=60, help='emulated seconds per run (60)')
    parser.add_argument('--runs', type=int, default=5, help='runs per engine, the median is reported (5)')
    parser.add_argument('--allow-software-gl', action='store_true')
    parser.add_argument('--json', help='write the result to this file')
    options = parser.parse_args()

    options.emulator = os.path.abspath(options.emulator)
    options.program = os.path.abspath(options.program)
    engines = [e for e in options.engines.split(',') if e]
    for engine in engines:
        if engine not in ('jit', 'interp'):
            sys.exit('perf: no engine %s' % engine)

    environment, gl = None, None
    if options.renderer == 'gl':
        environment, gl = gl_environment(options.allow_software_gl)

    result = {
        'date': datetime.datetime.now().astimezone().isoformat(timespec='seconds'),
        'machine': machine(),
        'emulator': options.emulator,
        'program': os.path.basename(options.program),
        'emulated_seconds': options.seconds,
        'runs_per_engine': options.runs,
        'results': [],
    }
    if gl is not None:
        result['glxinfo'] = {'accelerated': gl[0], 'renderer': gl[1],
                             'GALLIUM_DRIVER': environment.get('GALLIUM_DRIVER')}

    work = tempfile.mkdtemp(prefix='nulldc-perf-')
    try:
        for engine in engines:
            runs = []
            for i in range(options.runs):
                runs.append(run_once(options, engine, environment, os.path.join(work, '%s-%d' % (engine, i))))
                print('  %-6s run %d: %7.1f %%, CPU %7.1f %%  (load %.1f)' % (
                    engine, i + 1, runs[-1]['speed_pct'], runs[-1]['speed_cpu_pct'], runs[-1]['load_1m']),
                    file=sys.stderr)

            label = runs[0]['renderer']
            gl_renderer = runs[0]['gl_renderer']
            if options.renderer == 'gl':
                # what the emulator drew with decides the label, not what glxinfo promised
                if label != 'gles':
                    sys.exit('perf: the emulator used the renderer %s, not gles: not a -DHAS_TA_LLE=OFF build?' % label)
                software = gl_renderer is None or SOFTWARE_GL.search(gl_renderer) is not None
                if software and not options.allow_software_gl:
                    sys.exit('perf: the emulator drew with "%s", a software rasteriser: not reported' % gl_renderer)
                label = 'gl-software' if software else 'gl-gpu'

            speeds = [r['speed_pct'] for r in runs]
            cpu_speeds = [r['speed_cpu_pct'] for r in runs]
            result['results'].append({
                'engine': engine,
                'renderer': label,
                'gl_renderer': gl_renderer if options.renderer == 'gl' else None,
                'speed_pct_median': statistics.median(speeds),
                'speed_pct_min': min(speeds),
                'speed_pct_max': max(speeds),
                'speed_pct_runs': speeds,
                'speed_cpu_pct_median': statistics.median(cpu_speeds),
                'speed_cpu_pct_min': min(cpu_speeds),
                'speed_cpu_pct_max': max(cpu_speeds),
                'speed_cpu_pct_runs': cpu_speeds,
                'cpu_s_runs': [r['cpu_s'] for r in runs],
                'emu_s_runs': [r['emu_s'] for r in runs],
                'load_1m_runs': [r['load_1m'] for r in runs],
                'vblanks': runs[0]['vblanks'],
                'emu_s': runs[0]['emu_s'],
            })
    finally:
        shutil.rmtree(work, ignore_errors=True)

    print('%s, %g emulated seconds, median of %d runs, %s' % (result['program'], options.seconds, options.runs,
                                                              result['date']))
    print('%-8s %-12s %10s %10s %10s %14s %10s %10s   %s' % (
        'engine', 'renderer', 'median %', 'min %', 'max %', 'CPU median %', 'CPU min %', 'CPU max %', 'load (1 min)'))
    for r in result['results']:
        print('%-8s %-12s %10.1f %10.1f %10.1f %14.1f %10.1f %10.1f   %.1f - %.1f%s' % (
            r['engine'], r['renderer'], r['speed_pct_median'], r['speed_pct_min'], r['speed_pct_max'],
            r['speed_cpu_pct_median'], r['speed_cpu_pct_min'], r['speed_cpu_pct_max'],
            min(r['load_1m_runs']), max(r['load_1m_runs']),
            '   [%s]' % r['gl_renderer'] if r['gl_renderer'] else ''))

    if options.json:
        with open(options.json, 'w') as f:
            json.dump(result, f, indent=2)
            f.write('\n')
    return 0


if __name__ == '__main__':
    sys.exit(main())
