"""Compare commentary on the forecast pairs from the completed SA replay.

The saved manifest fixes the order and identity of each adjacent forecast pair.
The model-only comparison uses the original evidence. The revised-prompt
comparison rebuilds the same updates from saved numerical forecasts and counts.
Only --start sends requests. Each comparison has its own ledger and receipts.
"""
import argparse
from collections import Counter
from pathlib import Path
import sys

WORKSPACE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(WORKSPACE / 'uploads'))
from live_pipeline import benchmark, commentary
from live_pipeline.storage import fingerprint, read
from live_pipeline.trial import load_key


def load_baseline(folder, *, for_new_requests=False):
    manifest = read(folder / 'manifest.json')
    if for_new_requests and manifest['prompt_version'] != commentary.PROMPT_VERSION:
        raise ValueError('Restore the original prompt version for a model-only comparison.')
    selected = []
    for key, expected in manifest['cases'].items():
        packet = read(folder / 'evidence' / (key + '.json'))
        if fingerprint(packet) != expected or fingerprint([packet['reference_forecast_id'], packet['forecast_id']]) != key:
            raise ValueError(f'Saved evidence does not match the original benchmark for {key}.')
        selected.append((key, packet))
    return selected


def review_saved(selected, rows, prompt_version):
    # Use the original prose even when the old check rejected it. Retain its
    # original decision and cost on the local review copy for comparison.
    prepared = {key: commentary.prepare_packet(packet, prompt_version) for key, packet in selected}
    reviewed = []
    for old in rows:
        row = dict(old, original_status=old['status'], original_reason=old.get('reason'))
        answer = old.get('text') or old.get('unvalidated_response')
        if answer is not None:
            try:
                commentary.validate(answer, prepared[row['comparison']])
            except ValueError as error:
                row.update(status='unavailable', reason=str(error), unvalidated_response=answer)
                row.pop('text', None)
            else:
                row.update(status='complete', text=answer)
                row.pop('reason', None)
                row.pop('unvalidated_response', None)
        reviewed.append(row)
    return reviewed


def revised_prompt_trial(run, start):
    """Hold the forecast pairs, models and service settings fixed while testing
    the current commentary. Read prices and settings from the two prior trials
    so their cost and timing can be compared on the same basis."""
    selected = load_baseline(run / 'benchmark')
    if not selected:
        raise ValueError('The original SA benchmark contains no comparisons.')
    cases = Counter(key for key, _ in selected)
    models = []
    previous_cost = 0
    for model, folder in (('gpt-6.1-terra', 'benchmark'), ('gpt-6.2-sol', 'benchmark-sol62')):
        receipts = read(run / folder / 'benchmark.json')['results']
        rows = [r for r in receipts if r['model'] == model]
        if Counter(r['comparison'] for r in rows) != cases:
            raise ValueError(f'Expected one original {model} receipt per forecast pair.')
        settings = next(r['configuration'] for r in rows if r.get('configuration'))
        if (settings['model'], settings['service_tier'], settings['reasoning_effort']) != (model, 'default', 'low'):
            raise ValueError(f'Expected Standard speed and low reasoning in the original {model} trial.')
        models.append({k: settings[k] for k in ('model', 'service_tier', 'reasoning_effort', 'max_output_tokens',
                                                'input_usd_per_million', 'cached_input_usd_per_million', 'output_usd_per_million')})
        previous_cost += sum(r.get('estimated_usd') or 0 for r in rows)

    refreshed = [(key, benchmark.refresh_packet(run / 'packages', 'previous', packet)) for key, packet in selected]
    output = run / ('benchmark-' + commentary.PROMPT_VERSION)
    ceiling = 15
    print(f'{len(refreshed)} verified successive updates. Terra 6.1 and Sol 6.2, Standard/low. '
          f'Up to {len(refreshed)*len(models)} requests. Earlier receipts US${previous_cost:.2f}. '
          f'Combined ceiling US${ceiling}.', flush=True)
    print(f'Review destination: {output / "review.html"}', flush=True)
    if not start:
        print('Dry run complete. Add --start to begin or resume paid requests.')
        return
    if benchmark.has_pending_requests(refreshed, models, output) and not load_key(WORKSPACE):
        raise ValueError('Set OPENAI_API_KEY in uploads/.env to start this comparison.')
    benchmark.run_benchmark(refreshed, models, output, budget_usd=ceiling,
        on_progress=lambda done, total: print(f'Receipts: {done}/{total}', flush=True))


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--start', action='store_true', help='Start or resume the selected paid comparison.')
    parser.add_argument('--revised-prompt', action='store_true',
                        help='Test the current prompt with Terra 6.1 and Sol 6.2, up to US$15 combined.')
    args = parser.parse_args(argv)
    run = WORKSPACE / 'live_runs' / '2026sa-commentary-full-20261118'
    if args.revised_prompt:
        revised_prompt_trial(run, args.start)
        return
    baseline = run / 'benchmark'
    selected = load_baseline(baseline, for_new_requests=args.start)
    old = read(baseline / 'benchmark.json')
    reviewed = review_saved(selected, old['results'], read(baseline/'manifest.json')['prompt_version'])
    sol = [row for row in reviewed if row['model'] == 'gpt-6.1-sol']
    if not selected or Counter(row['comparison'] for row in sol) != Counter(key for key, _ in selected):
        raise ValueError('Expected one Sol 6.1 receipt per comparison.')
    # Take the settings from a successful original request so that the only
    # intentional request difference is the model version. Check the prices
    # before using them to reserve funds against the new ceiling.
    config = dict(next(row['configuration'] for row in sol if 'configuration' in row),
                  model='gpt-6.2-sol', request_timeout_seconds=180)
    if (config['service_tier'], config['reasoning_effort'], config['input_usd_per_million'],
        config['cached_input_usd_per_million'], config['output_usd_per_million']) != ('default', 'low', 2, .1, 10):
        raise ValueError('Unexpected settings or prices in the original Sol receipts.')
    output = run / 'benchmark-sol62'
    review = run / 'model-comparison'
    review.mkdir(exist_ok=True)
    print(f'{len(selected)} saved updates. Sol 6.1 estimated cost US${sum(r.get("estimated_usd") or 0 for r in sol):.2f}. '
          'Sol 6.2 ceiling US$7.', flush=True)
    if args.start:
        if benchmark.has_pending_requests(selected, [config], output) and not load_key(WORKSPACE):
            parser.error('Set OPENAI_API_KEY in uploads/.env to start this comparison.')
        new = benchmark.run_benchmark(selected, [config], output, budget_usd=7,
            on_progress=lambda done, total: print(f'Sol 6.2 receipts: {done}/{total}', flush=True))
    elif (output / 'benchmark.json').exists():
        new = read(output / 'benchmark.json')['results']
    else:
        new = []
    order = {key: i for i, (key, _) in enumerate(selected)}
    rows = sorted(reviewed + new, key=lambda row: (order[row['comparison']], row['model']))
    # This cross-model review has no spending authority. Keep the independent
    # original and new budget ceilings in their own benchmark directories.
    benchmark.write_review(review, rows, None, selected)
    for model in sorted({row['model'] for row in rows}):
        print(model, dict(Counter(row['status'] for row in rows if row['model'] == model)))
    print(review / 'review.html')


if __name__ == '__main__':
    main()
