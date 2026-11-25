"""Public seat calls and possible booth errors for one automated forecast run.

Calls follow the published sequence, so a restart or a longer commentary window
needs the original date of each call. Booth warnings describe the current
forecast's treatment of reported preferences and expire with that evidence.
"""
from collections import defaultdict
import math


def active_calls(summary, evidence, previous):
    """Open a call only after actual FP or TCP votes have arrived in the district.
    The two percentage thresholds match LiveSnapshot::updateSeatWinCalled.
    A call can survive small fluctuations on later captures. Missing/nonfinite
    measurements preserve the previous assessment; a missing party in a
    populated win distribution has zero simulated wins."""
    counted = set()
    for booth in evidence.get('booths', {}).values():
        fp = booth.get('fp_votes_current', {})
        tcp = booth.get('tcp_votes_current', {})
        if any(v > 0 for v in fp.values()) or any(v > 0 for v in tcp.values()):
            counted.add(booth['seat_name'])
    counted.update(seat for seat, row in summary.get('published_district_tcp', {}).items()
                   if row.get('positive_units', 0) > 0)
    calls = dict(previous)
    for seat, district in summary['seats'].items():
        wins = district['win_percent']
        if not wins:
            continue
        if seat in calls:
            chance = wins.get(calls[seat]['party'], 0)
            if chance is None or not math.isfinite(chance) or chance >= 99:
                continue
            del calls[seat]
        if seat in counted and not summary.get('origin'):
            for party, chance in wins.items():
                if chance is not None and 99.995 < chance <= 100:
                    calls[seat] = {'party': party, 'since': summary['forecast_id']}
                    break
    return calls


def call_changes(previous, current):
    """Compare active calls at the two ends. A call since withdrawn is absent
    at the end, so it cannot be announced as a gain over this period. An older
    standing call that disappeared is a withdrawal. A fresh call after a
    withdrawal keeps the forecast ID of the fresh call."""
    old, new = (summary.get('seat_calls', {}) for summary in (previous, current))
    announced = sorted(((call['since'], seat, call['party']) for seat, call in new.items()
                        if old.get(seat) != call), key=lambda row: row[1])
    announced.sort(key=lambda row: row[0], reverse=True)
    groups = defaultdict(list)
    omitted = defaultdict(int)
    for identity, seat, party in announced:
        # Name every call in the latest update. Fill any remaining places
        # from that party's earlier calls, most recent first.
        if len(groups[('called', party)]) < 5 or identity == current['forecast_id']:
            groups[('called', party)].append(seat)
        else:
            omitted[('called', party)] += 1
    for seat, call in sorted(old.items()):
        if call['party'] != new.get(seat, {}).get('party'):
            groups[('withdrawn', call['party'])].append(seat)
    return [dict(action=key[0], party=key[1], seats=groups[key], others=omitted[key])
            for key in sorted(groups.keys() | omitted.keys())]


def booth_warnings(analysis):
    """Read the probability of the correction directed towards comparable
    booths. Routine rechecks can affect ordinary booths too, so their frequency
    gives no evidence of an anomaly. Use the unrounded probability for the
    boundaries; it estimates a model decision, not a confirmed counting error."""
    result = {}
    for district in analysis.get('preference_rechecking', {}).get('seats', []):
        rows = []
        for booth in district['booths']:
            probability = booth.get('broad_probability')
            if probability is None or not .5 <= probability <= 1:
                continue
            level = 'high' if probability > .98 else 'medium' if probability >= .8 else 'low'
            rows.append({'booth': booth['booth'], 'level': level, 'confidencePercent': 100 * probability})
        if rows:
            result[district['seat']] = sorted(rows, key=lambda r: (-r['confidencePercent'], r['booth']))
    return result


def noteworthy_warnings(previous, current):
    """For commentary select two new or reclassified medium/high warnings.
    Give closer races priority, then the higher confidence estimate. The seat
    page can always show every current warning, including the low band."""
    earlier = previous.get('booth_warnings', {})
    rows = []
    for seat, warnings in current.get('booth_warnings', {}).items():
        old = {row['booth']: row['level'] for row in earlier.get(seat, [])}
        chances = current['seats'].get(seat, {}).get('win_percent', {})
        if not chances:
            continue
        favourite = max((v for v in chances.values() if v is not None and math.isfinite(v)), default=100)
        for row in warnings:
            if row['level'] != 'low' and old.get(row['booth']) != row['level']:
                rows.append((favourite, seat, row))
    rows.sort(key=lambda item: (item[0], -item[2]['confidencePercent'], item[1], item[2]['booth']))
    return [dict(seat=seat, **row) for _, seat, row in rows[:2]]
