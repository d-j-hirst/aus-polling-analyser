"""Define reader-facing counting days and retain their last published reports.

Feed timestamps still use the election's timezone. Daily commentary instead
uses Sydney's clock by default: a day runs from 5 am to the next 5 am. These
are editorial boundaries, not changes to votes or forecast calculations.
"""
from datetime import datetime, timedelta, time

from .storage import atomic, read
from .timezones import ZoneInfo


def schedule(config):
    result = {'timezone': config.get('reporting_timezone', 'Australia/Sydney'),
              'day_start_hour': config.get('day_start_hour', 5),
              'three_hour_start_hour': config.get('three_hour_start_hour', 13)}
    ZoneInfo(result['timezone'])
    for field in ('day_start_hour', 'three_hour_start_hour'):
        if type(result[field]) is not int or not 0 <= result[field] <= 23:
            raise ValueError(field+' must be an hour from 0 to 23')
    if result['three_hour_start_hour'] <= result['day_start_hour']:
        raise ValueError('The afternoon window must start after the daily boundary')
    return result


def period(now, config):
    """Return calendar boundaries, never an assumed 24-hour duration.

    Construct each boundary in its own local date so daylight-saving changes
    can produce a 23- or 25-hour day. Before 5 am, the previous reporting day
    is still in progress; the explicit report_date makes that visible.
    """
    policy = schedule(config)
    zone = ZoneInfo(policy['timezone'])
    local = now.astimezone(zone)
    date = local.date()
    if local.hour < policy['day_start_hour']:
        date -= timedelta(days=1)
    start = datetime.combine(date, time(policy['day_start_hour']), zone)
    previous = datetime.combine(date-timedelta(days=1), time(policy['day_start_hour']), zone)
    return {'report_date': date.isoformat(), 'timezone': policy['timezone'],
            'start': start, 'previous_start': previous,
            'three_hour_enabled': local.hour >= policy['three_hour_start_hour']}


def yesterday(folder, current, summaries, config):
    """Keep yesterday's last report by reference, with no new paid request.

    On the first forecast of a new reporting day, freeze which earlier forecast
    ended yesterday. Late deliveries cannot rewrite that choice. Publications
    and their evidence already remain in this run, so a link to the original
    window preserves its text and facts without copying diagnostics each day.
    The first election night uses its since-start report as the daily report.
    """
    from .comparisons import instant
    day = period(instant(current['source_time'], config['timezone']), config)
    report_date = day['previous_start'].date().isoformat()
    path = folder/'daily-reports'/f'{report_date}.json'
    saved = read(path) if path.exists() else None
    # An older package can finish commentary after a newer one. It must not
    # inherit a selection made with forecasts that did not yet exist for it.
    if saved is None or saved['selected_by_forecast_id'] > current['forecast_id']:
        eligible = [s for s in summaries if not s.get('origin')
                    and s['forecast_id'] < current['forecast_id']
                    and day['previous_start'] <= instant(s['source_time'], config['timezone']) < day['start']]
        selected = max(eligible, key=lambda s: (instant(s['source_time'], config['timezone']), s['forecast_id'])) if eligible else None
        record = {'report_date': report_date, 'timezone': day['timezone'],
                  'day_start': day['previous_start'].isoformat(), 'day_end': day['start'].isoformat(),
                  'selected_by_forecast_id': current['forecast_id']}
        if selected:
            record.update(source_forecast_id=selected['forecast_id'],
                          source_time=instant(selected['source_time'], config['timezone']).isoformat())
        if saved is None:
            atomic(path, record)
    else:
        record = saved
    identity = record.get('source_forecast_id')
    if not identity:
        return dict(record, available=False, reason='No forecast was calculated during yesterday\'s reporting day.')
    package = folder/'packages'/identity
    if not (package/'ready-publication.json').exists():
        return dict(record, available=False, reason='Yesterday\'s last forecast has not finished publication preparation.')
    publication = read(package/'publication.json')
    window = 'today'
    group = publication['comparisons'].get(window)
    if group is None and report_date == config['election_date']:
        window = 'since_start'
        group = publication['comparisons'].get(window)
    if not group or not group.get('available'):
        return dict(record, available=False, reason='Yesterday\'s last forecast has no complete daily comparison.')
    return dict(record, available=True, source_window=window,
                reference_forecast_id=group['reference_forecast_id'],
                reference_source_time=group.get('reference_source_time'),
                actual_seconds=group.get('actual_seconds'),
                commentary_status=publication['commentary'].get(window, {}).get('status', 'unavailable'))
