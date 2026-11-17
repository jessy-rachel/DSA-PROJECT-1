"""Week 3 experiments: compile C simulator, save CSV, and create plots.
Run inside repo root: python3 run_week3.py
Plotting requires: python3 -m pip install matplotlib
"""
import csv
import subprocess
import sys
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parent
EXEC = ROOT / 'week3_experiment'
COLUMNS = ['method', 'K', 'failure_denominator', 'seed', 'pass',
           'fram_writes', 'fram_reads', 'recovery_steps', 'max_log_size',
           'extra_steps', 'power_failures', 'checkpoint_failures',
           'full_log_events', 'checkpoints']
KS = (10, 50, 100, 500)
# 0=never, 100000=rare, 20000=medium, 5000=frequent
RATES = (0, 100000, 20000, 5000)
SEEDS = range(1, 31)  # 30 runs per combination; change to range(1, 101) for 100


def build():
    command = ['gcc', '-std=c11', '-O2', '-Wall', '-Wextra',
               str(ROOT / 'week3_experiment.c'), '-o', str(EXEC)]
    subprocess.run(command, cwd=ROOT, check=True)


def run_one(method, k, denominator, seed, capacity=None):
    command = [str(EXEC), method, str(k), str(denominator), str(seed)]
    if capacity is not None:
        command.append(str(capacity))
    result = subprocess.run(command, capture_output=True, text=True, cwd=ROOT)
    if result.returncode != 0:
        raise RuntimeError('Run failed: ' + ' '.join(command) + '\n' + result.stderr + result.stdout)
    values = next(csv.reader([result.stdout.strip()]))
    row = dict(zip(COLUMNS, values))
    return {k: (v if k == 'method' else int(v)) for k, v in row.items()}


def write_rows(filename, rows):
    with (ROOT / filename).open('w', newline='') as file:
        writer = csv.DictWriter(file, fieldnames=COLUMNS)
        writer.writeheader()
        writer.writerows(rows)
    print(f'Created {filename}: {len(rows)} runs')


def write_summary(rows):
    groups = defaultdict(list)
    for row in rows:
        groups[(row['method'], row['K'], row['failure_denominator'])].append(row)
    fields = ['method', 'K', 'failure_denominator', 'runs', 'pass_rate',
              'mean_fram_writes', 'mean_fram_reads', 'mean_recovery_steps',
              'max_log_size', 'mean_extra_steps', 'mean_power_failures',
              'checkpoint_failure_runs']
    summary = []
    for (method, k, rate), group in sorted(groups.items()):
        avg = lambda key: round(sum(r[key] for r in group) / len(group), 2)
        summary.append(dict(method=method, K=k, failure_denominator=rate,
            runs=len(group), pass_rate=avg('pass'),
            mean_fram_writes=avg('fram_writes'),
            mean_fram_reads=avg('fram_reads'),
            mean_recovery_steps=avg('recovery_steps'),
            max_log_size=max(r['max_log_size'] for r in group),
            mean_extra_steps=avg('extra_steps'),
            mean_power_failures=avg('power_failures'),
            checkpoint_failure_runs=sum(r['checkpoint_failures'] > 0 for r in group)))
    with (ROOT / 'week3_summary.csv').open('w', newline='') as file:
        writer = csv.DictWriter(file, fieldnames=fields)
        writer.writeheader()
        writer.writerows(summary)
    print('Created week3_summary.csv')
    return summary


def plot(summary):
    try:
        import matplotlib.pyplot as plt
    except ImportError:
        print('Plots skipped: install matplotlib with python3 -m pip install matplotlib')
        return
    for metric, ylabel, filename in [
        ('mean_fram_writes', 'Mean FRAM writes', 'week3_writes.png'),
        ('mean_extra_steps', 'Mean extra sample attempts', 'week3_extra_steps.png'),
        ('mean_recovery_steps', 'Mean recovery log operations', 'week3_recovery.png'),
        ('max_log_size', 'Maximum log entries', 'week3_max_log.png'),
    ]:
        fig, ax = plt.subplots(figsize=(9, 5.5))
        for method in ('stack', 'queue'):
            for den in RATES:
                pts = sorted((r for r in summary if r['method'] == method and
                              r['failure_denominator'] == den), key=lambda r: r['K'])
                label = f'{method.title()} / ' + ('none' if den == 0 else f'1/{den:,}')
                ax.plot([p['K'] for p in pts], [p[metric] for p in pts], marker='o', label=label)
        ax.set_xlabel('Checkpoint interval K (samples)')
        ax.set_ylabel(ylabel)
        ax.set_title(f'{ylabel} versus checkpoint interval')
        ax.set_xticks(KS)
        ax.grid(True, alpha=0.3)
        ax.legend(fontsize=8, ncol=2)
        fig.tight_layout()
        fig.savefig(ROOT / filename, dpi=160)
        plt.close(fig)
        print(f'Created {filename}')


def main():
    build()
    rows = [run_one(method, k, rate, seed)
            for method in ('stack', 'queue')
            for k in KS for rate in RATES for seed in SEEDS]
    write_rows('week3_results.csv', rows)
    summary = write_summary(rows)
    plot(summary)

    # Explicitly test small logs filling up, with automatic early checkpoints.
    full = [run_one(method, 500, 20000, seed, capacity=5)
            for method in ('stack', 'queue') for seed in range(1, 11)]
    write_rows('week3_full_log_test.csv', full)
    assert all(r['full_log_events'] > 0 and r['pass'] for r in full)

    # Search deliberately for runs that crashed DURING checkpoint writes.
    crash = []
    for method in ('stack', 'queue'):
        for seed in range(1, 501):
            r = run_one(method, 50, 1000, seed)
            if r['checkpoint_failures']:
                crash.append(r)
                break
        else:
            raise RuntimeError(f'No checkpoint crash found for {method}')
    write_rows('week3_checkpoint_crashes.csv', crash)
    print('ALL TESTS PASS. Files are ready for report and plotting.')


if __name__ == '__main__':
    try:
        main()
    except (subprocess.CalledProcessError, RuntimeError) as error:
        sys.exit(str(error))
