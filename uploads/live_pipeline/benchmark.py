"""Compare commentary variants once each on saved numerical evidence.

A durable receipt belongs to a snapshot pair and model/speed/reasoning choice,
not a timeframe label. Restarting never repeats a paid attempt, including a call
whose response may have been lost. This benchmark does not publish reports.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
from contextlib import closing
import html
import json
from pathlib import Path
import sqlite3
import threading
import time

from . import commentary, commentary_format, comparisons
from .storage import atomic, read, fingerprint, run_lock


# The small model/speed trials still default to $10. The separately prepared
# whole-count, two-model replay explicitly selects $15 after the measured
# workload estimate. Every run continues to reserve cost before dispatch.
MAX_BUDGET_USD = 15


def validate_budget(value):
    if not 0 < value <= MAX_BUDGET_USD:
        raise ValueError(f'Aggregate benchmark ceiling must be positive and at most US${MAX_BUDGET_USD}')


def variant_identity(model):
    return {'model': model['model'], 'service_tier': model.get('service_tier', 'default'),
            'reasoning_effort': model.get('reasoning_effort', 'medium')}


def refresh_packet(packages, window, original):
    """Rebuild editorial evidence from the exact archived endpoints. The
    published comparison also records reversals and counting activity between
    them, which cannot be recovered by looking only at the two snapshots."""
    destination = packages/original['forecast_id']
    reference = packages/original['reference_forecast_id']
    saved = read(destination/'comparisons.json')[window]
    if saved['reference_forecast_id'] != original['reference_forecast_id']:
        raise ValueError('The archived comparison refers to a different forecast')
    current = read(destination/'summary.json')
    group = comparisons.compare(current, read(reference/'summary.json'), [],
                                read(destination/'evidence.json.gz'), read(reference/'evidence.json.gz'))
    for field in ('win_changes', 'count_activity', 'actual_seconds'):
        group[field] = saved[field]
    refreshed = comparisons.prompt_packet(current, group)
    for field in ('forecast_id', 'source_time', 'reference_forecast_id', 'reference_source_time'):
        if refreshed[field] != original[field]:
            raise ValueError(f'The archived {field} does not match the selected comparison')
    refreshed['source_timezone'] = original.get('source_timezone')
    return refreshed


def select_packets(folder, forecast_ids=None, reference_id=None, limit=8, refresh_evidence=False):
    packets = {}
    windows = {}
    packages = Path(folder)/'packages'
    for path in sorted(packages.glob('*/requests.json')):
        if forecast_ids and path.parent.name not in forecast_ids:
            continue
        for window, request in read(path).items():
            packet = request['packet']
            if reference_id and packet['reference_forecast_id'] != reference_id:
                continue
            pair = fingerprint([packet['reference_forecast_id'], packet['forecast_id']])
            if pair in packets and fingerprint(packets[pair]) != fingerprint(packet):
                raise ValueError('The same forecast pair has conflicting numerical evidence')
            packets[pair] = packet
            windows[pair] = window
    selected = list(packets.items())
    if len(selected) > limit:
        indices = [round(i*(len(selected)-1)/max(1, limit-1)) for i in range(limit)]
        selected = [selected[i] for i in indices]
    if refresh_evidence:
        return [(key, refresh_packet(packages, windows[key], packet)) for key, packet in selected]
    return selected


def validate_models(models):
    if not isinstance(models, list) or not 1 <= len(models) <= 24:
        raise ValueError('Configure between one and twenty-four model/speed/reasoning variants')
    if len({fingerprint(variant_identity(m)) for m in models}) != len(models):
        raise ValueError('A model/speed/reasoning variant is repeated')


def has_pending_requests(selected, models, output):
    """Previously completed or uncertain attempts need no credential to inspect."""
    calls = Path(output)/'calls'
    for key, _ in selected:
        for model in models:
            identity = fingerprint([key, variant_identity(model)])
            if not (calls/f'{identity}.json').exists() and not (calls/f'{identity}.started.json').exists():
                return True
    return False


def mechanical_review(packet):
    """Show the forecast changes and reporting units independently of LLM prose."""
    rows = []
    for row in packet['win_changes']:
        if abs(row['change_pp']) < .01:
            continue
        groups = packet.get('booth_changes', {}).get('by_seat', {}).get(row['seat'], {})
        booths = '; '.join(label+': '+groups[kind]['text'] for kind, label in [('new', 'New'), ('updated', 'Updated')]
                           if groups.get(kind, {}).get('text'))
        candidate = row.get('identity') or {}
        cells = [row['seat'], candidate.get('name') or candidate.get('party') or row['party_id'],
                 commentary_format.decimal(row['before'])+'%',
                 commentary_format.decimal(row['after'])+'% ('+commentary_format.decimal(row['change_pp'], True)+' pp)', booths]
        rows.append('<tr>'+''.join('<td>'+html.escape(c)+'</td>' for c in cells)+'</tr>')
    counting_rows = []
    # A correction in a safe seat can matter even with no movement in winning
    # chances. Keep its booth entry visible independently of the probability list.
    for seat, groups in packet.get('booth_changes', {}).get('by_seat', {}).items():
        cells = [seat, groups['new']['text'], groups['updated']['text']]
        counting_rows.append('<tr>'+''.join('<td>'+html.escape(c)+'</td>' for c in cells)+'</tr>')
    return '<h2>Mechanical changes</h2><p>Win chances, ordered by absolute percentage-point movement. Booth lists show up to three new and two significantly updated units per district, including declaration categories, ordered by affected votes.</p><table><tr><th>Seat</th><th>Candidate</th><th>Before</th><th>Current (change)</th><th>Counting</th></tr>'+''.join(rows)+'</table><h3>Counting updates</h3><table><tr><th>Seat</th><th>New</th><th>Updated</th></tr>'+''.join(counting_rows)+'</table>'


def write_review(output, results, budget, selected=()):
    """Provide a readable local comparison alongside complete machine receipts."""
    atomic(output/'benchmark.json', {'budget_ceiling_usd': budget, 'results': results,
           'deployment_model': None, 'decision': 'Review facts, explanations, cache usage, latency and cost before selecting a model.'})
    rows, sections = [], []
    escape = lambda value: html.escape(str(value))
    # A variant now has several responses. Name the exact reference and current
    # forecasts beside timings and prose, so a warmed second update cannot be
    # mistaken for another response to the first update. Times use the feed's
    # election-local clock; they are not the benchmark's execution times.
    comparisons = {}
    for index, (key, packet) in enumerate(selected, 1):
        reference = packet.get('reference_source_time') or 'pre-count reference'
        current = packet.get('source_time') or 'unknown source time'
        comparisons[key] = (f'Update {index}: {reference} to {current} '
                            f'({packet.get("source_timezone", "election local time")}; '
                            f'{packet["reference_forecast_id"]} to {packet["forecast_id"]})')
    for index, result in enumerate(results, 1):
        label = ' / '.join(str(result.get(k, '?')) for k in ('model', 'service_tier', 'reasoning_effort'))
        comparison = comparisons.get(result.get('comparison'), result.get('comparison', 'Not recorded'))
        usage = result.get('usage') or {}
        details = usage.get('input_tokens_details') or {}
        tokens, cached = usage.get('input_tokens', 0), details.get('cached_tokens', 0)
        cost = '$'+format(result['estimated_usd'], '.4f') if result.get('estimated_usd') is not None else 'reserved / unknown'
        cells = [label, comparison, result['status'], f"{result.get('latency_seconds', result.get('elapsed_seconds', 0)):.1f}s",
                 usage.get('service_tier', 'unknown'), f'{cached:,} / {tokens:,}',
                 details.get('cache_write_tokens', 0), usage.get('output_tokens', 'unknown'),
                 (usage.get('output_tokens_details') or {}).get('reasoning_tokens', 'unknown'), cost]
        rows.append(f'<tr><td><a href="#report-{index}">'+escape(label)+'</a></td>'+
                    ''.join('<td>'+escape(c)+'</td>' for c in cells[1:])+'</tr>')
        text = result.get('text') or result.get('unvalidated_response') or {}
        render = lambda value: commentary_format.render(value, result.get('entities', {}))
        body = ''
        if result.get('reason'):
            body += '<p><strong>Not accepted:</strong> '+escape(result['reason'])+'</p>'
        def render_block(value):
            if result.get('format') == commentary.LAYOUT:
                heading, *points = value.split('\n')
                if points and all(point.startswith('- ') for point in points):
                    return '<h3>'+render(heading)+'</h3><ul>'+''.join(
                        '<li>'+render(point[2:])+'</li>' for point in points)+'</ul>'
            return '<p>'+render(value)+'</p>'
        if 'key_changes' in text:
            # A rejected structured reply is available verbatim under details.
            # It has not passed the checks required to render public commentary.
            text = {}
        body += render_block(text.get('summary', 'No response available.'))
        for item in text.get('developments', []):
            if 'text' in item:
                body += render_block(item['text'])
                continue
            # Older benchmark receipts remain readable without rewriting their
            # original response or silently accepting a previously rejected call.
            body += '<p><b>'+escape(item['seat'])+'</b>: '+escape(item['explanation'])+'</p><ul>'
            for fact in item.get('facts', []):
                metric = result.get('metrics', {}).get(fact['metric_id'], {})
                context = result.get('metric_contexts', {}).get(metric.get('context_id'), {})
                body += '<li>'+escape(f"{context.get('seat', '')} {context.get('quantity', '')} {metric.get('path', '')}: {fact['value']}")+'</li>'
            body += '</ul>'
        if result.get('unvalidated_response') is not None:
            body += '<details><summary>Original response and formatting for inspection</summary><pre style="white-space:pre-wrap">'+escape(json.dumps(result['unvalidated_response'], indent=2, ensure_ascii=False))+'</pre></details>'
        sections.append(f'<section id="report-{index}"><h2>'+escape(label)+'</h2><p>'+escape(comparison)+'</p>'+body+'</section>')
    page = '''<!doctype html><html lang="en"><meta charset="utf-8"><title>SA commentary comparison</title>
<style>body{font:16px/1.5 system-ui;max-width:1250px;margin:32px auto;padding:0 20px;color:#192533}table{border-collapse:collapse;width:100%;font-size:14px}td,th{text-align:left;padding:8px;border-bottom:1px solid #ccc}section{border-top:2px solid #ddd;margin-top:28px}h2{font-size:20px}.badge{background:#e7ebf1;padding:2px 4px;border-radius:3px}</style>
<h1>SA commentary comparison</h1><p>Each variant receives each selected snapshot pair once, processing its successive updates together. Times measure the request, not waiting behind other variants. Cache reads can affect speed; a single trial cannot isolate the speed tier's effect. Numerical facts were checked, but explanations still need review. Nothing here has been uploaded.</p>
<table><thead><tr><th>Variant</th><th>Comparison</th><th>Status</th><th>Time</th><th>Served tier</th><th>Cached / input tokens</th><th>Cache writes</th><th>Output tokens</th><th>Reasoning tokens</th><th>Estimated cost</th></tr></thead><tbody>'''+''.join(rows)+'</tbody></table>'+''.join(sections)+''.join('<h2>'+escape(comparisons[k])+'</h2>'+mechanical_review(p) for k, p in selected)+'</html>'
    temporary = output/'review.html.tmp'
    temporary.write_text(page, encoding='utf-8')
    temporary.replace(output/'review.html')


def run_benchmark(selected, models, output, budget_usd=10, concurrency=3,
                  client_factory=None, on_progress=None, keep_running=None):
    """Run independent groups in parallel, then reuse model/effort prefixes.

    Each call gets its own measurement window. Waiting behind other variants
    does not time it out; normal 90/30-second publication targets are reported
    separately. There are no paid retries or warm-up calls.
    """
    validate_models(models)
    validate_budget(budget_usd)
    versions = {m.get('prompt_version', commentary.PROMPT_VERSION) for m in models}
    if len(versions) != 1 or not versions <= commentary.SUPPORTED_VERSIONS:
        raise ValueError('Use one supported prompt version per benchmark directory')
    output = Path(output)
    output.mkdir(parents=True, exist_ok=True)
    with run_lock(output):
        budget = commentary.Budget(output/'budget.sqlite', budget_usd)
        manifest = {'prompt_version': next(iter(versions)),
                    'cases': {key: fingerprint(packet) for key, packet in selected}}
        if (output/'manifest.json').exists() and read(output/'manifest.json') != manifest:
            raise ValueError('Benchmark evidence or prompt changed; keep this run intact and choose a new benchmark directory')
        atomic(output/'manifest.json', manifest)
        for key, packet in selected:
            atomic(output/'evidence'/f'{key}.json', packet)
        results = []
        forecast_windows = {}
        for _, packet in selected:
            forecast_windows.setdefault(packet['forecast_id'], set()).add(packet['reference_forecast_id'])
        progress_lock = threading.Lock()
        completed = 0

        def measure(model, key, packet):
            variant = variant_identity(model)
            identity = fingerprint([key, variant])
            receipt = output/'calls'/f'{identity}.json'
            started_file = output/'calls'/f'{identity}.started.json'
            if receipt.exists():
                return read(receipt)
            if started_file.exists():
                result = dict(variant, status='unavailable', reason='An earlier attempt was interrupted. It will not be sent again.', comparison=key)
                atomic(receipt, result)
                return result
            if keep_running and not keep_running():
                return dict(variant, status='unavailable', reason='Benchmark paused before dispatch.', comparison=key)
            started = time.time()
            # Persist before dispatch. An uncertain crash receipt can prevent a
            # useful retry, but cannot duplicate a possibly billed request.
            atomic(started_file, {'at_unix': started, 'variant': variant, 'comparison': key})
            # An adjacent-update trial uses each current overview once per
            # variant. Only pay to cache it when another window can reuse it.
            settings = dict(model, cache_comparison=False, cache_overview=len(forecast_windows[packet['forecast_id']]) > 1)
            try:
                result = commentary.generate(packet, settings, budget,
                    started+model.get('request_timeout_seconds', 180),
                    client_factory(model) if client_factory else None)
            except Exception as error:
                result = {'status': 'unavailable', 'reason': str(error)}
                # Failed/incomplete responses still cost money. Look up this
                # request's receipt rather than a concurrent call's latest row.
                with closing(sqlite3.connect(budget.path)) as database:
                    supplied = commentary.prepare_packet(packet)
                    row = database.execute('SELECT cost,detail FROM calls WHERE request_key=? ORDER BY id DESC LIMIT 1',
                        (commentary.request_identity(supplied, settings),)).fetchone()
                if row:
                    detail = json.loads(row[1] or '{}')
                    result.update(usage=detail.get('usage', {}), estimated_usd=row[0],
                                  unvalidated_response=detail.get('unvalidated_response'),
                                  entities=commentary_format.entities(packet))
            elapsed = time.time()-started
            result.update(variant, comparison=key, elapsed_seconds=elapsed,
                          within_state_deadline=elapsed <= 90, within_federal_deadline=elapsed <= 30)
            atomic(receipt, result)
            return result

        groups = {}
        for model in models:
            groups.setdefault((model['model'], model.get('reasoning_effort', 'medium')), []).append(model)

        def group_work(group):
            nonlocal completed
            # Rotate speed order: Fast should not always benefit from Standard
            # warming the prefix. Actual cached tokens remain visible per call.
            offset = {'low': 0, 'medium': 1, 'high': 2}.get(group[0].get('reasoning_effort'), 0) % len(group)
            order = group[offset:]+group[:offset]
            # Keep successive updates together for this exact configuration.
            # This directly measures reusable election context even if caches
            # are not shared across different reasoning or speed settings.
            measured = []
            for model in order:
                for key, packet in selected:
                    measured.append(measure(model, key, packet))
                    # A whole-election group can take much longer than a small
                    # trial. Report each durable receipt instead of appearing
                    # idle until one model has processed the entire election.
                    with progress_lock:
                        completed += 1
                        if on_progress:
                            on_progress(completed, len(models)*len(selected))
            return measured

        with ThreadPoolExecutor(max_workers=max(1, min(4, concurrency))) as executor:
            futures = [executor.submit(group_work, group) for group in groups.values()]
            for future in as_completed(futures):
                results.extend(future.result())
                write_review(output, results, budget_usd, selected)
        return results


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--run', required=True, type=Path)
    parser.add_argument('--models', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--budget-usd', type=float, default=10)
    parser.add_argument('--limit', type=int, default=8)
    parser.add_argument('--forecast-id', action='append')
    parser.add_argument('--reference-forecast-id')
    parser.add_argument('--refresh-evidence', action='store_true',
                        help='Use current evidence selection on the saved forecast counts and window history.')
    parser.add_argument('--confirm-paid', action='store_true')
    parser.add_argument('--dry-run', action='store_true')
    args = parser.parse_args(argv)
    if not args.confirm_paid and not args.dry_run:
        parser.error('Review costs, then pass --confirm-paid to make paid API calls.')
    try:
        validate_budget(args.budget_usd)
    except ValueError as error:
        parser.error(str(error))
    if args.limit < 1:
        parser.error('Limit must be positive')
    models = read(args.models)
    validate_models(models)
    if read(args.run/'run.json')['election'] != '2026sa':
        parser.error('The initial benchmark uses saved 2026sa evidence')
    selected = select_packets(args.run, args.forecast_id, args.reference_forecast_id, args.limit, args.refresh_evidence)
    if not selected:
        parser.error('No prepared commentary evidence packages are available')
    if args.dry_run:
        total = 0
        for model in models:
            maximum = sum(commentary.maximum_cost(commentary.prepare_packet(p), model) for _, p in selected)
            total += maximum
            print(model['model'], model.get('service_tier', 'default'), model.get('reasoning_effort', 'medium'),
                  '%d comparisons; conservative reservation US$%.2f' % (len(selected), maximum))
        print('All cold-call reservations: US$%.2f; aggregate ceiling: US$%.2f. No requests sent.' % (total, args.budget_usd))
        return
    run_benchmark(selected, models, args.output, args.budget_usd)


if __name__ == '__main__':
    main()
