"""Normalize curated final pre-election turnout publications.

This adapter preserves the last useful update available before polls closed.
It complements commission adapters where the original operational files are
not retained. Exact published counts remain exact; counts reconstructed from
one-decimal district rates are explicitly approximate.

Main functions:
* ``download_election_files`` downloads fixed-version district tables.
* ``build_observations`` combines curated state totals with district evidence.
* ``merge_dataset`` replaces only records previously written by this adapter.
* ``main`` supports selected elections, reporting and atomic publication.
"""

import argparse
import csv
from dataclasses import dataclass
from decimal import Decimal, InvalidOperation, ROUND_HALF_UP
import io
import json
from pathlib import Path
import re
from urllib.request import Request, urlopen

import turnout_data


ANALYSIS_DIRECTORY = Path(__file__).resolve().parent
DEFAULT_OUTPUT_DIRECTORY = ANALYSIS_DIRECTORY / 'Data' / 'Turnout'
DOWNLOAD_TIMEOUT_SECONDS = 30
ADAPTER_ID = 'published-election-eve-operational-v1'

PREPOLL_MEASURE = 'prepoll_votes_cast_cumulative'
AEC_PREPOLL_MEASURE = 'prepoll_votes_issued_cumulative'
POSTAL_APPLICATION_MEASURE = 'postal_applications_cumulative'
POSTAL_ISSUED_MEASURE = 'postal_ballots_issued_cumulative'
POSTAL_RETURN_MEASURE = 'postal_votes_returned_cumulative'
POSTAL_ACCEPTED_MEASURE = 'postal_votes_accepted_cumulative'
PREPOLL_READY_MEASURE = 'prepoll_votes_ready_for_election_night_count'
POSTAL_READY_MEASURE = 'postal_votes_ready_for_election_night_count'
EARLY_VOTES_RECORDED_MEASURE = 'early_and_postal_votes_recorded_cumulative'
PRE_ELECTION_VOTES_CAST_MEASURE = 'pre_election_votes_cast_cumulative'
REMOTE_ELECTRONIC_MEASURE = 'remote_electronic_votes_cast_cumulative'


@dataclass(frozen=True)
class PublishedSource:
    source_id: str
    authority: str
    locator: str
    notes: str = ''


@dataclass(frozen=True)
class PublishedCount:
    measure: str
    observed_at: str
    count: int
    source_category: str
    count_precision: str = 'exact'
    count_relation: str = 'equal'
    derivation: str = 'direct'
    observation_status: str = 'contemporaneous'
    source_id: str = ''
    count_basis: str = 'reported'


@dataclass(frozen=True)
class PublishedElection:
    election_code: str
    election_date: str
    article_url: str
    state_counts: tuple
    district_url: str = ''
    district_layout: str = ''
    district_observed_at: str = ''
    geography_basis: str = 'state'
    additional_sources: tuple = ()


ELECTIONS = {
    '2004fed': PublishedElection(
        election_code='2004fed',
        election_date='2004-10-09',
        article_url='https://results.aec.gov.au/12246/polling.htm',
        state_counts=(PublishedCount(
            POSTAL_ISSUED_MEASURE,
            '2004-10-08',
            760000,
            'Almost 760,000 postal votes issued',
            count_precision='approximate',
            observation_status='final_reconciled',
            source_id='aec-2004-behind-the-scenes',
        ),),
        geography_basis='national',
        additional_sources=(PublishedSource(
            source_id='aec-2004-behind-the-scenes',
            authority='Australian Electoral Commission',
            locator='https://results.aec.gov.au/12246/polling.htm',
            notes=(
                'Later official account of the election-period postal issue '
                'total; published as a rounded figure.'
            ),
        ),),
    ),
    '2006qld': PublishedElection(
        election_code='2006qld',
        election_date='2006-09-09',
        article_url='https://www.queenslandjudgments.com.au/caselaw/qsc/2009/294',
        state_counts=(PublishedCount(
            POSTAL_APPLICATION_MEASURE,
            '2006-09-07',
            141000,
            'About 141,000 postal-vote applications received',
            count_precision='approximate',
            observation_status='final_reconciled',
            source_id='qsc-2009-ecq-postal-evidence',
        ),),
        additional_sources=(PublishedSource(
            source_id='qsc-2009-ecq-postal-evidence',
            authority=(
                'Supreme Court of Queensland, recording Electoral '
                'Commission of Queensland evidence'
            ),
            locator='https://www.queenslandjudgments.com.au/caselaw/qsc/2009/294',
            notes=(
                'Later judgment recording ECQ evidence about total '
                'applications; the published figure is rounded.'
            ),
        ),),
    ),
    '2006vic': PublishedElection(
        election_code='2006vic',
        election_date='2006-11-25',
        article_url=(
            'https://www.vec.vic.gov.au/-/media/'
            'f2c09bfff28349918caa42745fec83e1.pdf'
        ),
        state_counts=(
            PublishedCount(
                PREPOLL_MEASURE,
                '2006-11-24',
                255161,
                'Early votes cast at early voting centres',
                observation_status='final_reconciled',
                source_id='vec-2006-annual-report',
            ),
            PublishedCount(
                POSTAL_APPLICATION_MEASURE,
                '2006-11-23',
                226170,
                'Postal-vote applications processed',
                observation_status='final_reconciled',
                source_id='vec-2006-annual-report',
            ),
        ),
        additional_sources=(PublishedSource(
            source_id='vec-2006-annual-report',
            authority='Victorian Electoral Commission',
            locator=(
                'https://www.vec.vic.gov.au/-/media/'
                'f2c09bfff28349918caa42745fec83e1.pdf'
            ),
            notes=(
                'Later official annual report recording final election-period '
                'early-vote and postal-application totals.'
            ),
        ),),
    ),
    '2007fed': PublishedElection(
        election_code='2007fed',
        election_date='2007-11-24',
        article_url=(
            'https://aphref.aph.gov.au/house/committee/em/elect07/subs/'
            'sub169.pdf'
        ),
        state_counts=(
            PublishedCount(
                POSTAL_APPLICATION_MEASURE,
                '2007-11-22',
                833178,
                'Postal vote applications received',
                observation_status='final_reconciled',
                source_id='aec-2007-jscem-submission',
            ),
            PublishedCount(
                POSTAL_ISSUED_MEASURE,
                '2007-11-22',
                812826,
                'Postal voting packages issued within Australia',
                observation_status='final_reconciled',
                source_id='aec-2007-jscem-submission',
            ),
        ),
        geography_basis='national',
        additional_sources=(PublishedSource(
            source_id='aec-2007-jscem-submission',
            authority='Australian Electoral Commission',
            locator=(
                'https://aphref.aph.gov.au/house/committee/em/elect07/'
                'subs/sub169.pdf'
            ),
            notes=(
                'Later AEC submission reconstructing final election-period '
                'postal application and package issue totals.'
            ),
        ),),
    ),
    '2008wa': PublishedElection(
        election_code='2008wa',
        election_date='2008-09-06',
        article_url=(
            'https://www.elections.wa.gov.au/sites/default/files/content/'
            'documents/2008_SGE_Report.pdf'
        ),
        state_counts=(
            PublishedCount(
                POSTAL_ISSUED_MEASURE,
                '2008-09-04',
                81219,
                'Early-by-post votes requested and issued',
                observation_status='final_reconciled',
                source_id='waec-2008-election-report',
            ),
            PublishedCount(
                POSTAL_READY_MEASURE,
                '2008-09-06T18:00:00+08:00',
                35467,
                'Postal votes admitted for election-night counting',
                observation_status='final_reconciled',
                source_id='waec-2008-election-report',
            ),
        ),
        additional_sources=(PublishedSource(
            source_id='waec-2008-election-report',
            authority='Western Australian Electoral Commission',
            locator=(
                'https://www.elections.wa.gov.au/sites/default/files/'
                'content/documents/2008_SGE_Report.pdf'
            ),
            notes=(
                'Later official election report reconstructing postal '
                'issuance and election-night readiness, not a retained '
                'contemporaneous snapshot.'
            ),
        ),),
    ),
    '2009qld': PublishedElection(
        election_code='2009qld',
        election_date='2009-03-21',
        article_url=(
            'https://www.abc.net.au/news/2009-03-21/'
            'polls-open-for-qld-election/1626004'
        ),
        state_counts=(PublishedCount(
            POSTAL_APPLICATION_MEASURE,
            '2009-03-21T12:07:00+10:00',
            213000,
            'About 213,000 postal-vote requests received',
            count_precision='approximate',
            source_id='abc-2009qld-election-day',
        ),),
        additional_sources=(PublishedSource(
            source_id='abc-2009qld-election-day',
            authority=(
                'ABC News, quoting Queensland Electoral Commissioner '
                'David Kerslake'
            ),
            locator=(
                'https://www.abc.net.au/news/2009-03-21/'
                'polls-open-for-qld-election/1626004'
            ),
            notes='Election-day report updated at 12:07pm AEST.',
        ),),
    ),
    '2010sa': PublishedElection(
        election_code='2010sa',
        election_date='2010-03-20',
        article_url=(
            'https://www.abc.net.au/news/2010-03-16/'
            'game-on-for-sa-politics/365834'
        ),
        state_counts=(PublishedCount(
            POSTAL_APPLICATION_MEASURE,
            '2010-03-14',
            80000,
            'More than 80,000 postal-vote applications received',
            count_precision='approximate',
            count_relation='lower_bound',
            source_id='abc-2010sa-postal-update',
        ),),
        additional_sources=(PublishedSource(
            source_id='abc-2010sa-postal-update',
            authority='ABC News, reporting ECSA figures',
            locator=(
                'https://www.abc.net.au/news/2010-03-16/'
                'game-on-for-sa-politics/365834'
            ),
            notes=(
                'Latest retained numeric update found before polling day; '
                'the reported count refers to the preceding weekend.'
            ),
        ),),
    ),
    '2013wa': PublishedElection(
        election_code='2013wa',
        election_date='2013-03-09',
        article_url=(
            'https://www.abc.net.au/news/2013-03-09/'
            '2013-wa-election---some-notes-on-interpeting-abc-election-'
            'result/9388140'
        ),
        state_counts=(
            PublishedCount(
                PREPOLL_READY_MEASURE,
                '2013-03-08',
                78000,
                'Pre-poll votes processed and ready for election-night count',
            ),
            PublishedCount(
                POSTAL_READY_MEASURE,
                '2013-03-08',
                45000,
                'Postal votes processed and ready for election-night count',
            ),
        ),
    ),
    '2014vic': PublishedElection(
        election_code='2014vic',
        election_date='2014-11-29',
        article_url=(
            'https://www.abc.net.au/news/2014-11-28/'
            'victorian-election-2014---early-and-postal-vote-turnouts-by-elec/'
            '9388518'
        ),
        state_counts=(),
        district_url=(
            'https://www.abc.net.au/news/2014-11-28/'
            'victorian-election-2014---early-and-postal-vote-turnouts-by-elec/'
            '9388518'
        ),
        district_layout='vic2014-combined-rates-article',
        district_observed_at='2014-11-28T18:00:00+11:00',
    ),
    '2015qld': PublishedElection(
        election_code='2015qld',
        election_date='2015-01-31',
        article_url=(
            'https://www.ecq.qld.gov.au/_resource/documents/pdf/about-us/'
            'publications/annual-reports/2014-15-Annual-Report.pdf'
        ),
        state_counts=(PublishedCount(
            POSTAL_ISSUED_MEASURE,
            '2015-01-28',
            306064,
            'Postal votes mailed by the central postal-voting project',
            observation_status='final_reconciled',
            source_id='ecq-2014-15-annual-report',
        ),),
        additional_sources=(PublishedSource(
            source_id='ecq-2014-15-annual-report',
            authority='Electoral Commission of Queensland',
            locator=(
                'https://www.ecq.qld.gov.au/_resource/documents/pdf/'
                'about-us/publications/annual-reports/'
                '2014-15-Annual-Report.pdf'
            ),
            notes=(
                'Later official annual report recording the exact central '
                'postal mailout total; duplicate applications are excluded.'
            ),
        ),),
    ),
    '2014sa': PublishedElection(
        election_code='2014sa',
        election_date='2014-03-15',
        article_url=(
            'https://antonygreen.com.au/'
            '2022-sa-election-pre-poll-and-postal-voting-rates/'
        ),
        state_counts=(PublishedCount(
            PREPOLL_MEASURE,
            '2014-03-14',
            80087,
            'Retrospective final in-state pre-poll total',
            observation_status='final_reconciled',
        ),),
    ),
    '2017wa': PublishedElection(
        election_code='2017wa',
        election_date='2017-03-11',
        article_url=(
            'https://www.abc.net.au/news/2017-03-10/'
            'wa-election-things-you-need-to-know-before-you-vote/8332860'
        ),
        state_counts=(
            PublishedCount(
                PREPOLL_MEASURE,
                '2017-03-10T19:14:00+08:00',
                180000,
                'WAEC expected final early voting to exceed 180,000',
                count_precision='approximate',
                count_relation='lower_bound',
                source_id='abc-2017wa-election-eve',
                count_basis='forecast',
            ),
            PublishedCount(
                POSTAL_APPLICATION_MEASURE,
                '2017-03-07T08:21:00+08:00',
                160513,
                'Postal vote applications received before applications closed',
                source_id='abc-2017wa-postal-update',
            ),
        ),
        additional_sources=(
            PublishedSource(
                source_id='abc-2017wa-election-eve',
                authority='ABC News, reporting WA Electoral Commission figures',
                locator=(
                    'https://www.abc.net.au/news/2017-03-10/'
                    'wa-election-things-you-need-to-know-before-you-vote/'
                    '8332860'
                ),
                notes=(
                    'Election-eve WAEC forecast, not a completed final '
                    'pre-poll count.'
                ),
            ),
            PublishedSource(
                source_id='abc-2017wa-postal-update',
                authority='ABC News, reporting WA Electoral Commission figures',
                locator=(
                    'https://www.abc.net.au/news/2017-03-07/'
                    'wa-election-sees-record-number-of-early-voting-'
                    'applications/8333474'
                ),
                notes=(
                    'Latest surviving exact postal-application update found; '
                    'applications remained open until the following day.'
                ),
            ),
        ),
    ),
    '2017qld': PublishedElection(
        election_code='2017qld',
        election_date='2017-11-25',
        article_url=(
            'https://www.abc.net.au/news/2017-11-25/'
            'qld-election-state-go-to-the-polls/9191428'
        ),
        state_counts=(
            PublishedCount(
                PREPOLL_MEASURE,
                '2017-11-25T07:58:00+10:00',
                717000,
                'About 717,000 people had cast pre-poll votes',
                count_precision='approximate',
            ),
            PublishedCount(
                POSTAL_ISSUED_MEASURE,
                '2017-11-25T07:58:00+10:00',
                369000,
                'About 369,000 postal voters; ballots returned were fewer',
                count_precision='approximate',
            ),
        ),
    ),
    '2018sa': PublishedElection(
        election_code='2018sa',
        election_date='2018-03-17',
        article_url=(
            'https://antonygreen.com.au/'
            '2022-sa-election-pre-poll-and-postal-voting-rates/'
        ),
        state_counts=(
            PublishedCount(
                PREPOLL_MEASURE,
                '2018-03-16',
                120468,
                'Retrospective final pre-poll total',
                observation_status='final_reconciled',
            ),
            PublishedCount(
                POSTAL_APPLICATION_MEASURE,
                '2018-03-16',
                82213,
                'Retrospective postal applications received',
                observation_status='final_reconciled',
            ),
            PublishedCount(
                POSTAL_ISSUED_MEASURE,
                '2018-03-16',
                94831,
                'Accepted applications plus permanent postal electors',
                derivation='sum_published_counts',
                observation_status='final_reconciled',
            ),
        ),
    ),
    '2018vic': PublishedElection(
        election_code='2018vic',
        election_date='2018-11-24',
        article_url=(
            'https://antonygreen.com.au/'
            'tracking-the-early-vote-for-the-2022-victorian-election/'
        ),
        state_counts=(
            PublishedCount(
                PREPOLL_MEASURE,
                '2018-11-23',
                1389980,
                'Retrospective final pre-poll total',
                observation_status='final_reconciled',
            ),
            PublishedCount(
                POSTAL_APPLICATION_MEASURE,
                '2018-11-21',
                383921,
                'Retrospective final postal application total',
                observation_status='final_reconciled',
            ),
            PublishedCount(
                EARLY_VOTES_RECORDED_MEASURE,
                '2018-11-23',
                1600000,
                'More than 1.6 million early and postal votes cast',
                count_precision='approximate',
                count_relation='lower_bound',
                source_id='abc-2018vic-election-eve',
            ),
        ),
        additional_sources=(PublishedSource(
            source_id='abc-2018vic-election-eve',
            authority='ABC News, reporting Victorian election figures',
            locator=(
                'https://www.abc.net.au/news/2018-11-23/'
                'victorian-election-early-votes-counting-result/10529776'
            ),
            notes=(
                'Contemporaneous election-eve lower bound for combined early '
                'and postal votes; the publication did not provide an exact '
                'combined count.'
            ),
        ),),
    ),
    '2019nsw': PublishedElection(
        election_code='2019nsw',
        election_date='2019-03-23',
        article_url=(
            'https://www.abc.net.au/news/2019-03-23/'
            'nsw-election-polls-open-premier-daley-make-last-ditch-pitch/'
            '10932594'
        ),
        state_counts=(
            PublishedCount(
                PRE_ELECTION_VOTES_CAST_MEASURE,
                '2019-03-22',
                1300000,
                'More than 1.3 million people voted before election day',
                count_precision='approximate',
                count_relation='lower_bound',
                source_id='abc-2019nsw-election-eve',
            ),
            PublishedCount(
                REMOTE_ELECTRONIC_MEASURE,
                '2019-03-22',
                220000,
                '220,000 people used iVote',
                count_precision='approximate',
                source_id='abc-2019nsw-election-eve',
            ),
        ),
        additional_sources=(PublishedSource(
            source_id='abc-2019nsw-election-eve',
            authority='ABC News, reporting NSW Electoral Commission figures',
            locator=(
                'https://www.abc.net.au/news/2019-03-23/'
                'nsw-election-polls-open-premier-daley-make-last-ditch-pitch/'
                '10932594'
            ),
            notes=(
                'Final contemporaneous report of pre-election voting. The '
                'combined total is published only as a lower bound and the '
                'iVote count is rounded to the nearest thousand.'
            ),
        ),),
    ),
    '2020qld': PublishedElection(
        election_code='2020qld',
        election_date='2020-10-31',
        article_url=(
            'https://antonygreen.com.au/'
            '2020-queensland-election-tracking-the-early-vote/'
        ),
        state_counts=(
            PublishedCount(
                PREPOLL_MEASURE,
                '2020-10-30',
                1288696,
                'Final pre-poll votes taken',
            ),
            PublishedCount(
                POSTAL_ISSUED_MEASURE,
                '2020-10-31T10:30:00+10:00',
                905806,
                'Postal vote packs dispatched',
            ),
            PublishedCount(
                POSTAL_RETURN_MEASURE,
                '2020-10-30T18:00:00+10:00',
                571095,
                'Postal envelopes returned',
            ),
            PublishedCount(
                POSTAL_ACCEPTED_MEASURE,
                '2020-10-31T10:30:00+10:00',
                329334,
                'Postal envelopes admitted to the count',
            ),
            PublishedCount(
                PREPOLL_READY_MEASURE,
                '2020-10-31T10:30:00+10:00',
                925000,
                'Around 925,000 pre-poll votes available election night',
                count_precision='approximate',
            ),
            PublishedCount(
                POSTAL_READY_MEASURE,
                '2020-10-31T10:30:00+10:00',
                320000,
                'At least 320,000 postal votes available election night',
                count_precision='approximate',
                count_relation='lower_bound',
            ),
        ),
        district_url='https://datawrapper.dwcdn.net/OZgDp/7/',
        district_layout='qld2020-postal-chart',
        district_observed_at='2020-10-31T10:30:00+10:00',
    ),
    '2024qld': PublishedElection(
        election_code='2024qld',
        election_date='2024-10-26',
        article_url=(
            'https://antonygreen.com.au/'
            'early-voting-and-how-tonights-queensland-election-night-count-'
            'might-unfold/'
        ),
        state_counts=(
            PublishedCount(
                PREPOLL_MEASURE,
                '2024-10-26',
                1620434,
                'Final early voting total',
            ),
            PublishedCount(
                POSTAL_ISSUED_MEASURE,
                '2024-10-26',
                692180,
                'Postal applications processed and packs dispatched',
            ),
            PublishedCount(
                POSTAL_RETURN_MEASURE,
                '2024-10-26',
                338476,
                'Derived from 48.9% of 692,180 dispatched postal packs',
                count_precision='approximate',
                derivation='rounded_rate_times_reference_count',
            ),
            PublishedCount(
                PREPOLL_READY_MEASURE,
                '2024-10-26',
                1215326,
                'Around three-quarters of final pre-polls countable election night',
                count_precision='approximate',
                derivation='rounded_rate_times_reference_count',
            ),
        ),
    ),
    '2021wa': PublishedElection(
        election_code='2021wa',
        election_date='2021-03-13',
        article_url=(
            'https://antonygreen.com.au/'
            '2021-wa-election-tracking-the-early-vote/'
        ),
        state_counts=(
            PublishedCount(
                PREPOLL_MEASURE,
                '2021-03-12',
                585774,
                'Final pre-poll votes taken',
            ),
            PublishedCount(
                POSTAL_APPLICATION_MEASURE,
                '2021-03-12',
                331078,
                'Postal vote applications received',
            ),
            PublishedCount(
                POSTAL_RETURN_MEASURE,
                '2021-03-12',
                169301,
                'Postal votes returned and processed',
            ),
        ),
        district_url='https://datawrapper.dwcdn.net/BoTbq/7/',
        district_layout='wa2021-rates-chart',
        district_observed_at='2021-03-12',
    ),
    '2025wa': PublishedElection(
        election_code='2025wa',
        election_date='2025-03-08',
        article_url=(
            'https://antonygreen.com.au/'
            'wa2025-tracking-the-early-voting-statistics/'
        ),
        state_counts=(
            PublishedCount(
                PREPOLL_MEASURE,
                '2025-03-07',
                664850,
                'Thursday total 557,693 plus final-Friday count 107,157',
                derivation='sum_published_counts',
            ),
            PublishedCount(
                POSTAL_APPLICATION_MEASURE,
                '2025-03-06',
                215000,
                'Around 215,000 postal applications received',
                count_precision='approximate',
            ),
            PublishedCount(
                POSTAL_READY_MEASURE,
                '2025-03-05',
                130000,
                'Around 130,000 returned postals ready for election-night count',
                count_precision='approximate',
            ),
        ),
    ),
    '2022sa': PublishedElection(
        election_code='2022sa',
        election_date='2022-03-19',
        article_url=(
            'https://antonygreen.com.au/'
            '2022-sa-election-pre-poll-and-postal-voting-rates/'
        ),
        state_counts=(
            PublishedCount(
                PREPOLL_MEASURE,
                '2022-03-18',
                208136,
                'Final pre-poll votes cast',
            ),
            PublishedCount(
                POSTAL_APPLICATION_MEASURE,
                '2022-03-18',
                170081,
                'Postal applications including permanent postal voters',
            ),
        ),
        district_url='https://datawrapper.dwcdn.net/ytpvX/7/dataset.csv',
        district_layout='sa-rates',
        district_observed_at='2022-03-18',
    ),
    '2022fed': PublishedElection(
        election_code='2022fed',
        election_date='2022-05-21',
        article_url='https://antonygreen.com.au/5647-2/',
        state_counts=(
            PublishedCount(
                AEC_PREPOLL_MEASURE,
                '2022-05-20',
                5541757,
                'Final total in the reported daily pre-poll figures',
            ),
            PublishedCount(
                POSTAL_APPLICATION_MEASURE,
                '2022-05-20',
                2731060,
                'Postal applications received by the election-eve update',
            ),
            PublishedCount(
                POSTAL_RETURN_MEASURE,
                '2022-05-20',
                1644061,
                'Latest returns on election eve; Friday returns unavailable',
            ),
        ),
        geography_basis='national',
    ),
    '2022vic': PublishedElection(
        election_code='2022vic',
        election_date='2022-11-26',
        article_url=(
            'https://antonygreen.com.au/'
            'tracking-the-early-vote-for-the-2022-victorian-election/'
        ),
        state_counts=(
            PublishedCount(
                PREPOLL_MEASURE,
                '2022-11-24',
                1908400,
                'Final pre-poll votes cast',
            ),
            PublishedCount(
                POSTAL_APPLICATION_MEASURE,
                '2022-11-24',
                586208,
                'Postal vote applications processed',
            ),
            PublishedCount(
                POSTAL_RETURN_MEASURE,
                '2022-11-24',
                272779,
                'Postal votes returned',
            ),
        ),
        district_url='https://datawrapper.dwcdn.net/QydzN/6/dataset.csv',
        district_layout='vic-rates',
        district_observed_at='2022-11-24',
    ),
    '2023nsw': PublishedElection(
        election_code='2023nsw',
        election_date='2023-03-25',
        article_url=(
            'https://antonygreen.com.au/'
            'nsw2023-pre-poll-and-postal-vote-application-rates-by-district/'
        ),
        state_counts=(
            PublishedCount(
                PREPOLL_MEASURE,
                '2023-03-24',
                1566493,
                'Final pre-poll votes cast',
            ),
            PublishedCount(
                POSTAL_ISSUED_MEASURE,
                '2023-03-24',
                540208,
                'Postal votes applied for and dispatched',
            ),
            PublishedCount(
                POSTAL_RETURN_MEASURE,
                '2023-03-24',
                92077,
                'Postal votes returned',
            ),
        ),
        district_url='https://datawrapper.dwcdn.net/SqI3D/2/dataset.csv',
        district_layout='nsw-rates',
        district_observed_at='2023-03-24',
    ),
}


DISTRICT_RATE_COLUMNS = {
    'nsw-rates': (
        ('Pre-Poll Voted', PREPOLL_MEASURE),
        ('Applied for Postal', POSTAL_ISSUED_MEASURE),
    ),
    'sa-rates': (
        ('Pre-Poll %', PREPOLL_MEASURE),
        ('Postal %', POSTAL_APPLICATION_MEASURE),
    ),
    'vic-rates': (
        ('PrePoll %', PREPOLL_MEASURE),
        ('Postal %', POSTAL_APPLICATION_MEASURE),
    ),
    'wa2021-rates-chart': (
        ('Pre-Poll Votes', PREPOLL_MEASURE),
        ('Postal Applications', POSTAL_APPLICATION_MEASURE),
        ('Postals Returned', POSTAL_RETURN_MEASURE),
    ),
}

DISTRICT_NAME_COLUMNS = {
    'wa2021-rates-chart': 'Electorate',
}


def _download(url):
    request = Request(
        url,
        headers={
            'User-Agent': (
                'AEF turnout research '
                '(https://www.aeforecasts.com/; aeforecasts@gmail.com)'
            )
        },
    )
    with urlopen(request, timeout=DOWNLOAD_TIMEOUT_SECONDS) as response:
        return response.read()


def download_election_files(election):
    """Download fixed-version district evidence configured for an election."""
    if not election.district_url:
        return {}
    return {'district': _download(election.district_url)}


def _seat_index(dataset):
    seats = {}
    for seat in dataset.seat_totals:
        if seat.enrolment is None:
            raise turnout_data.TurnoutDataError(
                '{} has no enrolment for election-eve rates'.format(
                    seat.seat_name
                )
            )
        seats[seat.seat_name] = seat
    return seats


def _parse_rate(value, label):
    try:
        rate = Decimal(value.strip())
    except (AttributeError, InvalidOperation):
        raise turnout_data.TurnoutDataError(
            '{} has invalid percentage {!r}'.format(label, value)
        )
    if not Decimal('0') <= rate <= Decimal('100'):
        raise turnout_data.TurnoutDataError(
            '{} percentage is outside 0-100'.format(label)
        )
    return rate


def _count_from_rate(enrolment, rate):
    return int(
        (Decimal(enrolment) * rate / Decimal('100')).quantize(
            Decimal('1'), rounding=ROUND_HALF_UP
        )
    )


def _csv_rows(data, label):
    try:
        text = data.decode('utf-8-sig')
    except UnicodeDecodeError as error:
        raise turnout_data.TurnoutDataError(
            '{} is not valid UTF-8: {}'.format(label, error)
        )
    try:
        dialect = csv.Sniffer().sniff(text[:2048], delimiters=',\t')
    except csv.Error as error:
        raise turnout_data.TurnoutDataError(
            '{} has no recognizable CSV delimiter: {}'.format(label, error)
        )
    rows = list(csv.DictReader(io.StringIO(text), dialect=dialect))
    if not rows:
        raise turnout_data.TurnoutDataError('{} has no data rows'.format(label))
    return rows


def _resolve_rate_district(name, layout, seats):
    if name in seats:
        return name
    if layout == 'vic-rates' and '{} District'.format(name) in seats:
        return '{} District'.format(name)
    if layout == 'vic-rates' and name == 'Narracan':
        # Narracan's election was postponed and is not part of 2022vic.json.
        return ''
    if name.lower() in {'state total', 'state-wide', 'total'}:
        return ''
    raise turnout_data.TurnoutDataError(
        '{} district table contains unknown district {!r}'.format(layout, name)
    )


def _parse_district_rates(election, data, dataset, source_id):
    seats = _seat_index(dataset)
    columns = DISTRICT_RATE_COLUMNS[election.district_layout]
    if election.district_layout.endswith('-chart'):
        data = _chart_data(
            data, '{} district chart'.format(election.election_code)
        )
    name_column = DISTRICT_NAME_COLUMNS.get(
        election.district_layout, 'District'
    )
    observations = []
    represented = set()
    for row_number, row in enumerate(
        _csv_rows(data, '{} district table'.format(election.election_code)),
        start=2,
    ):
        district = _resolve_rate_district(
            row.get(name_column, '').strip(), election.district_layout, seats
        )
        if not district:
            continue
        if district in represented:
            raise turnout_data.TurnoutDataError(
                '{} repeats district {}'.format(election.election_code, district)
            )
        represented.add(district)
        for column, measure in columns:
            if column not in row:
                raise turnout_data.TurnoutDataError(
                    '{} district table lacks {!r}'.format(
                        election.election_code, column
                    )
                )
            rate = _parse_rate(
                row[column],
                '{} row {} {}'.format(election.election_code, row_number, column),
            )
            observations.append(turnout_data.OperationalObservation(
                election_code=election.election_code,
                source_id=source_id,
                measure=measure,
                observed_at=election.district_observed_at,
                count=_count_from_rate(seats[district].enrolment, rate),
                geography_basis='elector_division',
                observation_status='contemporaneous',
                seat_name=district,
                source_category=(
                    'Published {} ({}% of enrolment)'.format(column, rate)
                ),
                count_precision='approximate',
                derivation='rounded_rate_times_enrolment',
            ))
    missing = set(seats) - represented
    if missing:
        raise turnout_data.TurnoutDataError(
            '{} district table omits: {}'.format(
                election.election_code, ', '.join(sorted(missing))
            )
        )
    return observations


def _chart_data(html, label):
    try:
        text = html.decode('utf-8')
    except UnicodeDecodeError as error:
        raise turnout_data.TurnoutDataError(
            '{} is not valid UTF-8: {}'.format(label, error)
        )
    match = re.search(r'"chartData":"((?:\\.|[^"\\])*)","isPreview"', text)
    nested = False
    if not match:
        match = re.search(
            r'\\"chartData\\":\\"((?:\\\\.|[^"\\])*)'
            r'\\",\\"isPreview\\"',
            text,
        )
        nested = match is not None
    if not match:
        raise turnout_data.TurnoutDataError(
            '{} contains no embedded chart data'.format(label)
        )
    try:
        chart_data = json.loads('"{}"'.format(match.group(1)))
        if nested:
            chart_data = chart_data.replace('\\r\\n', '\r\n').replace(
                '\\n', '\n'
            )
        return chart_data.encode('utf-8')
    except json.JSONDecodeError as error:
        raise turnout_data.TurnoutDataError(
            '{} has invalid embedded chart data: {}'.format(label, error)
        )


def _node_text(node):
    if not isinstance(node, dict):
        return ''
    if node.get('type') == 'text':
        return node.get('content', '')
    return ''.join(_node_text(child) for child in node.get('children', ()))


def _abc_article_tables(data, label):
    try:
        text = data.decode('utf-8')
    except UnicodeDecodeError as error:
        raise turnout_data.TurnoutDataError(
            '{} is not valid UTF-8: {}'.format(label, error)
        )
    match = re.search(
        r'<script id="__NEXT_DATA__"[^>]*>(.*?)</script>', text, re.DOTALL
    )
    if not match:
        raise turnout_data.TurnoutDataError(
            '{} contains no ABC article document'.format(label)
        )
    try:
        document = json.loads(match.group(1))
    except json.JSONDecodeError as error:
        raise turnout_data.TurnoutDataError(
            '{} has invalid ABC article data: {}'.format(label, error)
        )

    tables = []

    def collect(node):
        if isinstance(node, dict):
            if node.get('key') == 'table':
                tables.append(node)
            for value in node.values():
                collect(value)
        elif isinstance(node, list):
            for value in node:
                collect(value)

    collect(document)
    return tables


def _parse_vic2014_combined_rates(election, data, dataset, source_id):
    seats = _seat_index(dataset)
    label = '{} election-eve article'.format(election.election_code)
    matching_tables = [
        table for table in _abc_article_tables(data, label)
        if 'Alphabetic List' in _node_text(table)
        and 'Descending Turnout %' in _node_text(table)
    ]
    if len(matching_tables) != 1:
        raise turnout_data.TurnoutDataError(
            '{} contains {} matching turnout tables'.format(
                label, len(matching_tables)
            )
        )

    body = next(
        (child for child in matching_tables[0].get('children', ())
         if child.get('key') == 'tbody'),
        None,
    )
    if body is None:
        raise turnout_data.TurnoutDataError(
            '{} turnout table has no body'.format(label)
        )

    observations = []
    represented = set()
    for row_number, row in enumerate(body.get('children', ()), start=1):
        cells = [
            _node_text(cell).strip() for cell in row.get('children', ())
            if cell.get('key') in {'td', 'th'}
        ]
        if len(cells) < 2 or cells[:2] == ['Pct', 'Electorate']:
            continue
        if cells[1] == 'State Total':
            continue
        rate = _parse_rate(cells[0], '{} row {}'.format(label, row_number))
        district = '{} District'.format(cells[1])
        if district not in seats:
            raise turnout_data.TurnoutDataError(
                '{} row {} contains unknown district {!r}'.format(
                    election.election_code, row_number, cells[1]
                )
            )
        if district in represented:
            raise turnout_data.TurnoutDataError(
                '{} repeats district {}'.format(election.election_code, district)
            )
        represented.add(district)
        observations.append(turnout_data.OperationalObservation(
            election_code=election.election_code,
            source_id=source_id,
            measure=EARLY_VOTES_RECORDED_MEASURE,
            observed_at=election.district_observed_at,
            count=_count_from_rate(seats[district].enrolment, rate),
            geography_basis='elector_division',
            observation_status='contemporaneous',
            seat_name=district,
            source_category=(
                'Postal votes received plus pre-poll votes cast '
                '({}% of enrolment)'.format(rate)
            ),
            count_precision='approximate',
            derivation='rounded_rate_times_enrolment',
        ))

    missing = set(seats) - represented
    if missing:
        raise turnout_data.TurnoutDataError(
            '{} turnout table omits: {}'.format(
                election.election_code, ', '.join(sorted(missing))
            )
        )
    return observations


def _parse_qld2020_postal(election, data, dataset, source_id):
    seats = _seat_index(dataset)
    rows = _csv_rows(
        _chart_data(data, '{} district chart'.format(election.election_code)),
        '{} district chart data'.format(election.election_code),
    )
    observations = []
    represented = set()
    for row_number, row in enumerate(rows, start=2):
        district = row.get('District', '').strip()
        if district == 'Total':
            continue
        if district not in seats:
            raise turnout_data.TurnoutDataError(
                '{} row {} contains unknown district {!r}'.format(
                    election.election_code, row_number, district
                )
            )
        represented.add(district)
        for column, measure, category in (
            ('Sent', POSTAL_ISSUED_MEASURE, 'Postal vote packs dispatched'),
            ('Returned', POSTAL_RETURN_MEASURE, 'Postal envelopes returned'),
        ):
            try:
                count = int(row[column])
            except (KeyError, ValueError):
                raise turnout_data.TurnoutDataError(
                    '{} row {} has invalid {}'.format(
                        election.election_code, row_number, column
                    )
                )
            observations.append(turnout_data.OperationalObservation(
                election_code=election.election_code,
                source_id=source_id,
                measure=measure,
                observed_at=election.district_observed_at,
                count=count,
                geography_basis='elector_division',
                observation_status='contemporaneous',
                seat_name=district,
                source_category=category,
            ))
    missing = set(seats) - represented
    if missing:
        raise turnout_data.TurnoutDataError(
            '{} district table omits: {}'.format(
                election.election_code, ', '.join(sorted(missing))
            )
        )
    return observations


def build_observations(election, downloaded_files, dataset):
    """Build and validate one publication's state and district evidence."""
    default_source_id = 'antony-green-{}-election-eve'.format(
        election.election_code
    )
    source_specs = {source.source_id: source
                    for source in election.additional_sources}
    uses_default_source = bool(election.district_layout) or any(
        not count.source_id for count in election.state_counts
    )
    sources = []
    if uses_default_source:
        sources.append(turnout_data.SourceDefinition(
            source_id=default_source_id,
            election_code=election.election_code,
            authority='Antony Green, based on electoral commission data',
            locator=election.article_url,
            adapter=ADAPTER_ID,
            status='operational',
            category_regime='published-election-eve-v1',
            notes=(
                'Final pre-election controls; some older values survive only '
                'in a later retrospective publication. District rates are '
                'retained as approximate counts derived from final enrolment.'
            ),
        ))
    for source in election.additional_sources:
        sources.append(turnout_data.SourceDefinition(
            source_id=source.source_id,
            election_code=election.election_code,
            authority=source.authority,
            locator=source.locator,
            adapter=ADAPTER_ID,
            status='operational',
            category_regime='published-election-eve-v1',
            notes=source.notes,
        ))
    unknown_source_ids = {
        count.source_id for count in election.state_counts
        if count.source_id and count.source_id not in source_specs
    }
    if unknown_source_ids:
        raise turnout_data.TurnoutDataError(
            '{} counts reference unknown published sources: {}'.format(
                election.election_code,
                ', '.join(sorted(unknown_source_ids)),
            )
        )
    observations = [
        turnout_data.OperationalObservation(
            election_code=election.election_code,
            source_id=count.source_id or default_source_id,
            measure=count.measure,
            observed_at=count.observed_at,
            count=count.count,
            geography_basis=election.geography_basis,
            observation_status=count.observation_status,
            source_category=count.source_category,
            count_precision=count.count_precision,
            count_relation=count.count_relation,
            derivation=count.derivation,
            count_basis=count.count_basis,
        )
        for count in election.state_counts
    ]
    if election.district_layout in DISTRICT_RATE_COLUMNS:
        observations.extend(_parse_district_rates(
            election,
            downloaded_files['district'],
            dataset,
            default_source_id,
        ))
    elif election.district_layout == 'vic2014-combined-rates-article':
        observations.extend(_parse_vic2014_combined_rates(
            election,
            downloaded_files['district'],
            dataset,
            default_source_id,
        ))
    elif election.district_layout == 'qld2020-postal-chart':
        observations.extend(_parse_qld2020_postal(
            election,
            downloaded_files['district'],
            dataset,
            default_source_id,
        ))
    elif election.district_layout:
        raise turnout_data.TurnoutDataError(
            'unsupported district layout {}'.format(election.district_layout)
        )
    return sources, observations


def merge_dataset(dataset, election, downloaded_files):
    """Replace this adapter's prior records while retaining other evidence."""
    if len(dataset.elections) != 1:
        raise turnout_data.TurnoutDataError(
            '{} must contain exactly one election'.format(election.election_code)
        )
    definition = dataset.elections[0]
    if (
        definition.election_code != election.election_code
        or definition.election_date != election.election_date
    ):
        raise turnout_data.TurnoutDataError(
            'turnout file does not identify {}'.format(election.election_code)
        )
    replaced_source_ids = {
        source.source_id for source in dataset.sources
        if source.adapter == ADAPTER_ID
    }
    dataset.sources = [
        source for source in dataset.sources
        if source.source_id not in replaced_source_ids
    ]
    dataset.operational_observations = [
        observation for observation in dataset.operational_observations
        if observation.source_id not in replaced_source_ids
    ]
    sources, observations = build_observations(
        election, downloaded_files, dataset
    )
    dataset.sources.extend(sources)
    dataset.operational_observations.extend(observations)
    dataset.operational_observations.sort(key=lambda record: (
        record.observed_at,
        record.measure,
        record.seat_name,
        record.source_id,
    ))
    dataset.validate()
    return dataset


def coverage_summary(dataset):
    source_ids = {
        source.source_id for source in dataset.sources
        if source.adapter == ADAPTER_ID
    }
    records = [
        observation for observation in dataset.operational_observations
        if observation.source_id in source_ids
    ]
    return {
        'records': len(records),
        'aggregate': sum(not record.seat_name for record in records),
        'district': sum(bool(record.seat_name) for record in records),
        'approximate': sum(
            record.count_precision == 'approximate' for record in records
        ),
    }


def parse_args(args=None):
    parser = argparse.ArgumentParser(
        description='Normalize curated final pre-election turnout evidence.'
    )
    parser.add_argument(
        '--election',
        action='append',
        choices=tuple(ELECTIONS) + ('all',),
        required=True,
        help='Election to acquire; repeat for multiple elections or use all.',
    )
    parser.add_argument(
        '--output-directory',
        type=Path,
        default=DEFAULT_OUTPUT_DIRECTORY,
    )
    parser.add_argument(
        '--dry-run',
        action='store_true',
        help='Download and validate without replacing normalized files.',
    )
    return parser.parse_args(args)


def main(args=None):
    options = parse_args(args)
    selected = list(ELECTIONS) if 'all' in options.election else options.election
    selected = list(dict.fromkeys(selected))
    for election_code in selected:
        election = ELECTIONS[election_code]
        output_path = options.output_directory / '{}.json'.format(election_code)
        print('Loading election-eve evidence for {}...'.format(election_code))
        downloaded_files = download_election_files(election)
        dataset = turnout_data.load_dataset(output_path)
        dataset = merge_dataset(dataset, election, downloaded_files)
        if not options.dry_run:
            turnout_data.write_dataset_atomically(output_path, dataset)
        summary = coverage_summary(dataset)
        print(
            '  {} records: {} aggregate, {} district, {} approximate'.format(
                summary['records'],
                summary['aggregate'],
                summary['district'],
                summary['approximate'],
            )
        )
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
