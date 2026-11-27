"""Read completed early voting and postal application counts from the 2026 VEC workbook.

Each dated block has a statewide row and cumulative figures for every district.
These are counts of voters or applications, before scrutiny. The turnout model
converts them to estimates of formal votes and divides early votes between
reporting locations separately.
"""

from datetime import datetime
import re
from urllib.parse import urljoin, urlsplit

from scripts.turnout.turnout_qld_operational import read_xlsx_rows
from scripts.turnout.turnout_vic import _download, _parse_html


PINNED_NAME = 'completed-voting-statistics.xlsx'
COLUMNS = 'BCDEFGHI'
HEADINGS = ('Date', 'Processed on the day', 'Progressive Total', 'Cancelled on the day',
            'Progressive Total', 'Returned', 'Progressive Total', 'Returned', 'Progressive Total')


def latest_workbook(page):
    """Follow the statistics page's Excel link, which may change with each edition."""
    links = _parse_html(_download(page), 'VEC voting statistics').links
    urls = {urljoin(page, link) for link in links if urlsplit(link).path.lower().endswith('.xlsx')}
    if len(urls) != 1:
        raise ValueError('Could not identify one workbook on the VEC voting statistics page. '
                         'Download the early and postal vote workbook and supply its path.')
    url, = urls
    if urlsplit(url).scheme != 'https' or urlsplit(url).hostname != urlsplit(page).hostname:
        raise ValueError('The voting statistics workbook link does not point to the VEC website.')
    return _download(url)


def _counts(row):
    """Read all eight daily and cumulative counters. A blank is unknown, even for a quiet day."""
    values = [row.get(col, '') for col in COLUMNS]
    if not all(value.isascii() and value.isdigit() for value in values):
        raise ValueError(f'Missing or invalid count on VEC row {row.get("A")!r}.')
    counts = dict(zip(COLUMNS, map(int, values)))
    if any(counts[daily] > counts[total] for daily, total in zip('BDFH', 'CEGI')):
        raise ValueError(f'Daily count exceeds cumulative count on VEC row {row["A"]!r}.')
    if counts['E'] > counts['C']:
        raise ValueError(f'Postal cancellations exceed processed applications on VEC row {row["A"]!r}.')
    return counts


def read_summary(data, names, settings):
    """Reconcile the last early-voting day's block, or the latest block available.

    The workbook warns that its cumulative columns cover only the selected date
    range. A workbook starting after applications began could reconcile perfectly
    while understating the postal total. Check the start as well as the last day.
    """
    sheet = 'State Election 2026'
    rows = read_xlsx_rows(data, 'VEC early and postal vote statistics', sheet)
    groups, headings = rows.get(10, {}), rows.get(11, {})
    if (rows.get(2, {}).get('A') != sheet
            or [groups.get(c) for c in 'BFH'] != ['Postal Vote Applications', 'Postal Votes', 'Early Votes']
            or tuple(headings.get(c) for c in 'ABCDEFGHI') != HEADINGS):
        raise ValueError('Unexpected election or column headings in VEC voting statistics.')
    date_text = r'(\d{2}/\d{2}/\d{4})'
    period = re.fullmatch('Date range: ' + date_text + r'\s*[-–—]\s*' + date_text, rows.get(3, {}).get('A', ''))
    if not period:
        raise ValueError('Missing coverage dates in VEC voting statistics.')
    start, end = (datetime.strptime(value, '%d/%m/%Y').date() for value in period.groups())
    if start.isoformat() != settings['period_start']:
        raise ValueError(f'Unexpected start of VEC reporting period: {start}.')
    issued = datetime.strptime(rows.get(4, {}).get('A', ''), 'As at %d/%m/%Y %I:%M %p')
    if issued.date() < end:
        raise ValueError('VEC workbook timestamp precedes its last reporting day.')
    days = [(i, datetime.strptime(row['A'], '%d/%m/%Y').date()) for i, row in rows.items()
            if re.fullmatch(date_text, row.get('A', ''))]
    if not days or days[-1][1] != end or [day for _, day in days] != sorted({day for _, day in days}):
        raise ValueError('VEC daily blocks do not match the reporting period.')
    # A Saturday morning edition may already contain a partial Saturday block.
    # Friday's block still measures the same completed early-voting population.
    target = datetime.fromisoformat(settings['early_close']).date()
    eligible = [(i, day) for i, day in days if day <= target]
    if not eligible:
        raise ValueError('No VEC daily block on or before the end of early voting.')
    first_row, through = eligible[-1]
    next_row = next((i for i, _ in days if i > first_row), max(rows) + 1)
    statewide = _counts(rows[first_row])
    districts = {}
    for i, row in rows.items():
        if not first_row < i < next_row:
            continue
        name = row.get('A', '')
        if not name or name == 'Total':
            continue
        if name not in names or name in districts:
            raise ValueError(f'Unexpected or repeated district in VEC voting statistics: {name}.')
        districts[name] = _counts(row)
    missing = set(names) - districts.keys()
    if missing:
        raise ValueError('Missing VEC voting statistics for ' + ', '.join(sorted(missing)))
    if any(sum(count[col] for count in districts.values()) != statewide[col] for col in COLUMNS):
        raise ValueError('The VEC district rows do not sum to the statewide figures.')
    return through, issued, statewide, districts


def completed_observations(data, names, settings, poll_close):
    """Make one early-vote and one net postal application observation per district.

    Friday's district totals are usable only after early voting closes. The
    publication time must also precede polling close so a replay can reproduce
    what could have been known when the live forecast began. Returned postals
    are recorded in the audit. More can still arrive after election day.
    """
    end, issued, statewide, districts = read_summary(data, names, settings)
    early_close = datetime.fromisoformat(settings['early_close'])
    if end != early_close.date() or issued < early_close or not statewide['H']:
        raise ValueError(f'VEC workbook covers {end} ({statewide["I"]:,} early votes). '
                         f'Waiting for completed district figures through {early_close.date()}.')
    if issued >= datetime.fromisoformat(poll_close):
        raise ValueError('This VEC edition was produced after polling closed.')
    evidence = []
    for name, counts in districts.items():
        common = dict(seat_name=name, geography_basis='elector_division',
                      observation_status='contemporaneous', observed_at=issued.isoformat())
        evidence.append(dict(common, measure='prepoll_votes_cast_cumulative', count=counts['I']))
        evidence.append(dict(common, measure='postal_applications_cumulative', count=counts['C'] - counts['E'],
                             processed=counts['C'], cancelled=counts['E'], returned=counts['G']))
    totals = dict(through=end.isoformat(), issued_at_victorian_time=issued.isoformat(),
                  districts=len(districts), early_votes=statewide['I'], postal_processed=statewide['C'],
                  postal_cancelled=statewide['E'], postal_applications_net=statewide['C'] - statewide['E'],
                  postal_returned=statewide['G'])
    return evidence, totals
