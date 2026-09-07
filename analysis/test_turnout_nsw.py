import unittest

import turnout_data
import turnout_nsw


def district_html(
    district='Example',
    enrolment=150,
    include_ivote=False,
    legacy_2015=False,
    postal_total=22,
):
    ordinary_label = (
        'Total Polling Place Ordinary Votes' if legacy_2015
        else 'Total Voting Centre Ordinary Votes'
    )
    early_label = (
        'Total Pre-Poll Ordinary Votes' if legacy_2015
        else 'Total Early Voting Centre Ordinary Votes'
    )
    enrolment_label = (
        'Enrolment' if legacy_2015 else 'Enrolment / Provisional'
    )
    categories = [
        (ordinary_label, 90, 5, 95, ''),
        (early_label, 20, 2, 22, ''),
        ('Absent', 4, 1, 5, 'Check Count Complete'),
        (enrolment_label, 1, 0, 1, 'Check Count Complete'),
    ]
    if include_ivote:
        categories.append(('iVote', 2, 0, 2, 'Check Count Complete'))
    categories.append(
        ('Postal', postal_total - 2, 2, postal_total, 'Check Count Complete')
    )
    if legacy_2015:
        categories.append(
            ('Provisional / Silent', 1, 0, 1, 'Check Count Complete')
        )
    totals = tuple(
        sum(row[index] for row in categories) for index in range(1, 4)
    )
    rows = ''.join(
        '<tr><td>{}</td><td>{}</td><td>{}</td><td>{}</td><td>{}</td></tr>'.format(
            *row
        )
        for row in categories
    )
    return (
        '<html><body>'
        '<h1>NSW STATE ELECTION RESULTS {year}</h1>'
        '<h3>State Electoral District of {district}</h3>'
        '<p>LA - Check Count Final Results</p>'
        '<p>First Preference Votes for each Candidate. Check Count Complete.</p>'
        '<p>Electors Enrolled as on 25/03/2023: {enrolment}</p>'
        '<table>'
        '<tr><th>Venue and Vote Types</th><th>Total Formal</th>'
        '<th>Informal</th><th>Total Votes/ Ballot Papers</th>'
        '<th>Count Status</th></tr>'
        '{rows}'
        '<tr><td>Total Votes / Ballot Papers</td><td>{formal}</td>'
        '<td>{informal}</td><td>{total}</td><td></td></tr>'
        '</table></body></html>'
    ).format(
        district=district,
        year='2015' if legacy_2015 else ('2019' if include_ivote else '2023'),
        enrolment=enrolment,
        rows=rows,
        formal=totals[0],
        informal=totals[1],
        total=totals[2],
    ).encode('utf-8')


class NswTurnoutAdapterTests(unittest.TestCase):
    election = turnout_nsw.NswElection(
        '2023nsw', '2023-03-25', 'TEST', expected_districts=1
    )
    url = 'https://example.test/TEST/LA/example/cc/fp_summary'

    def test_discovers_exact_district_coverage(self):
        page = (
            '<a href="/TEST/LA/example/cc/fp_summary">Example</a>'
        ).encode('utf-8')

        urls = turnout_nsw.discover_district_urls(self.election, page)

        self.assertEqual(len(urls), 1)
        self.assertTrue(urls[0].endswith('/TEST/LA/example/cc/fp_summary'))

    def test_builds_reconciled_vote_type_records(self):
        dataset = turnout_nsw.build_dataset(
            self.election, {self.url: district_html()}
        )

        self.assertEqual(len(dataset.seat_totals), 1)
        self.assertEqual(len(dataset.vote_types), 5)
        self.assertEqual(dataset.seat_totals[0].seat_name, 'Example')
        self.assertEqual(dataset.seat_totals[0].total_ballots, 145)
        self.assertEqual(
            dataset.vote_types[1].canonical_category,
            'early_combined',
        )
        self.assertEqual(
            dataset.vote_types[3].canonical_category,
            'enrolment_or_provisional',
        )

    def test_preserves_ivote_as_remote_electronic(self):
        election = turnout_nsw.NswElection(
            '2019nsw',
            '2019-03-23',
            'TEST',
            expected_districts=1,
            source_categories=turnout_nsw.NSW_2019_SOURCE_CATEGORIES,
        )

        dataset = turnout_nsw.build_dataset(
            election, {self.url: district_html(include_ivote=True)}
        )

        ivote = next(
            record for record in dataset.vote_types
            if record.source_category == 'iVote'
        )
        self.assertEqual(ivote.canonical_category, 'remote_electronic')

    def test_builds_2015_vote_type_partition(self):
        election = turnout_nsw.NswElection(
            '2015nsw',
            '2015-03-28',
            'SGE2015',
            expected_districts=1,
            results_path='la-home.htm',
            source_categories=turnout_nsw.NSW_2015_SOURCE_CATEGORIES,
        )
        url = (
            'https://example.test/SGE2015/la/example/cc/'
            'fp_summary/index.htm'
        )

        dataset = turnout_nsw.build_dataset(
            election,
            {url: district_html(include_ivote=True, legacy_2015=True)},
        )

        categories = {
            record.source_category: record.canonical_category
            for record in dataset.vote_types
        }
        self.assertEqual(
            categories['Total Polling Place Ordinary Votes'],
            'election_day_ordinary',
        )
        self.assertEqual(categories['Enrolment'], 'enrolment')
        self.assertEqual(categories['Provisional / Silent'], 'provisional')

    def test_rejects_vote_type_total_disagreement(self):
        page = district_html(postal_total=22).replace(
            b'<td>135</td><td>10</td><td>145</td>',
            b'<td>134</td><td>10</td><td>144</td>',
        )

        with self.assertRaises(turnout_data.TurnoutDataError):
            turnout_nsw.build_dataset(self.election, {self.url: page})

    def test_rejects_page_from_another_election(self):
        page = district_html().replace(b'RESULTS 2023', b'RESULTS 2019')

        with self.assertRaisesRegex(
            turnout_data.TurnoutDataError,
            'does not identify the requested election',
        ):
            turnout_nsw.build_dataset(self.election, {self.url: page})


if __name__ == '__main__':
    unittest.main()
