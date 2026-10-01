import unittest

import turnout_data
import turnout_vic


def district_html(
    year='2022', historical=False, postal_total=12, recount=False,
    old_categories=False,
):
    if old_categories:
        categories = [
            ('Ordinary Votes Total', 60, 30, 5, 95),
            ('Postal Votes', postal_total - 4, 4, 0, postal_total),
            ('Early Votes', 12, 8, 2, 22),
            ('Declaration Votes', 1, 0, 0, 1),
            ('Absent Votes', 3, 2, 0, 5),
        ]
    else:
        categories = [
            ('Ordinary Votes Total', 60, 30, 5, 95),
            ('Absent Votes', 3, 2, 0, 5),
            ('Early Votes', 12, 8, 2, 22),
            ('Marked As Voted Votes', 0, 0, 0, 0),
            ('Postal Votes', postal_total - 4, 4, 0, postal_total),
            ('Provisional Votes', 1, 0, 0, 1),
        ]
    formal = sum(row[1] + row[2] for row in categories)
    informal = sum(row[3] for row in categories)
    total = sum(row[4] for row in categories)
    rows = ''.join(
        '<tr><td>{}</td><td>{}</td><td>{}</td><td>{}</td><td>{}</td></tr>'.format(
            *row
        )
        for row in categories
    )
    if historical:
        title = (
            'Example District '
            'First Preference Results by Voting Centre'
        )
    else:
        title = (
            'Example District results by voting centre | '
            'Victorian Electoral Commission'
        )
    return (
        '<html><body><h1>{title}</h1><p>{result_kind} first preference votes</p>'
        '<p>Total Enrolment as at close of rolls: 150</p>'
        '<p>Formal Votes: {formal}</p><p>Informal Votes: {informal}</p>'
        '<p>Total Votes: {total}</p><table>'
        '<tr><th></th><th>Candidate One</th><th>Candidate Two</th>'
        '<th>Informal votes</th><th>Total votes polled</th></tr>'
        '<tr><th>Voting Centres</th><th>Party One</th><th>Party Two</th>'
        '<th>Informal votes</th><th>Total votes polled</th></tr>'
        '{rows}<tr><td>Total</td><td>{candidate_one}</td>'
        '<td>{candidate_two}</td><td>{informal}</td><td>{total}</td></tr>'
        '</table></body></html>'
    ).format(
        title=title,
        result_kind='Recount' if recount else 'Recheck',
        formal=formal,
        informal=informal,
        total=total,
        rows=rows,
        candidate_one=sum(row[1] for row in categories),
        candidate_two=sum(row[2] for row in categories),
    ).encode('utf-8')


class VicTurnoutAdapterTests(unittest.TestCase):
    current_election = turnout_vic.VicElection(
        '2022vic',
        '2022-11-26',
        'https://example.test/results-by-district',
        1,
        'current',
    )
    current_url = (
        'https://example.test/results-by-district/example-district-results/'
        'example-district-results-by-voting-centre'
    )

    def test_discovers_current_result_urls(self):
        index = (
            '<a href="/results-by-district/example-district-results">'
            'Example</a>'
        ).encode('utf-8')

        urls = turnout_vic.discover_district_urls(
            self.current_election, index
        )

        self.assertEqual(urls, [self.current_url])

    def test_discovers_historical_result_urls(self):
        election = turnout_vic.VicElection(
            '2018vic',
            '2018-11-24',
            'https://example.test/state2018/summary.html',
            1,
            'historical',
        )
        index = b'<a href="exampledistrict.html">Example</a>'

        urls = turnout_vic.discover_district_urls(election, index)

        self.assertEqual(
            urls,
            [
                'https://example.test/state2018/'
                'fpvbyvotingcentreexampledistrict.html'
            ],
        )

    def test_discovers_old_historical_result_urls(self):
        election = turnout_vic.VicElection(
            '2010vic',
            '2010-11-27',
            'https://example.test/state2010/state2010resultsummary.html',
            1,
            'historical',
        )
        index = b'<a href="state2010resultexampledistrict.html">Example</a>'

        urls = turnout_vic.discover_district_urls(election, index)

        self.assertEqual(
            urls,
            [
                'https://example.test/state2010/'
                'state2010fpvbyvotingcentreexampledistrict.html'
            ],
        )

    def test_uses_summary_page_when_recount_has_no_vote_types(self):
        election = turnout_vic.VicElection(
            '2006vic',
            '2006-11-25',
            'https://example.test/state2006/state2006resultsummary.html',
            1,
            'historical',
            ('Example District',),
            ('state2006resultexampledistrict.html',),
        )
        index = b'<a href="state2006resultexampledistrict.html">Example</a>'

        urls = turnout_vic.discover_district_urls(election, index)

        self.assertEqual(
            urls,
            ['https://example.test/state2006/state2006resultexampledistrict.html'],
        )

    def test_builds_reconciled_current_partition(self):
        dataset = turnout_vic.build_dataset(
            self.current_election,
            {self.current_url: district_html()},
        )

        self.assertEqual(len(dataset.seat_totals), 1)
        self.assertEqual(len(dataset.vote_types), 6)
        self.assertEqual(dataset.seat_totals[0].formal_votes, 128)
        self.assertEqual(dataset.seat_totals[0].informal_votes, 7)
        self.assertEqual(dataset.seat_totals[0].total_ballots, 135)
        categories = {
            record.canonical_category for record in dataset.vote_types
        }
        self.assertIn('early_combined', categories)
        self.assertIn('marked_as_voted', categories)

    def test_builds_historical_page_shape(self):
        election = turnout_vic.VicElection(
            '2018vic',
            '2018-11-24',
            'https://example.test/state2018/summary.html',
            1,
            'historical',
        )
        url = (
            'https://example.test/state2018/'
            'fpvbyvotingcentreexampledistrict.html'
        )

        dataset = turnout_vic.build_dataset(
            election, {url: district_html(year='2018', historical=True)}
        )

        self.assertEqual(dataset.seat_totals[0].seat_name, 'Example District')

    def test_accepts_final_historical_recount(self):
        election = turnout_vic.VicElection(
            '2018vic',
            '2018-11-24',
            'https://example.test/state2018/summary.html',
            1,
            'historical',
        )
        url = (
            'https://example.test/state2018/'
            'fpvbyvotingcentreexampledistrict.html'
        )

        dataset = turnout_vic.build_dataset(
            election,
            {url: district_html(year='2018', historical=True, recount=True)},
        )

        self.assertEqual(dataset.seat_totals[0].formal_votes, 128)

    def test_accepts_unrechecked_2006_primary_result(self):
        election = turnout_vic.VicElection(
            '2006vic',
            '2006-11-25',
            'https://example.test/state2006/state2006resultsummary.html',
            1,
            'historical',
        )
        url = (
            'https://example.test/state2006/'
            'state2006fpvbyvotingcentreexampledistrict.html'
        )
        page = district_html(
            year='2006', historical=True, old_categories=True
        ).replace(
            b'Recheck first preference votes', b'Primary votes'
        )

        dataset = turnout_vic.build_dataset(election, {url: page})

        self.assertEqual(len(dataset.vote_types), 5)
        self.assertIn(
            'declaration_combined',
            {record.canonical_category for record in dataset.vote_types},
        )

    def test_retains_recount_total_without_unavailable_vote_types(self):
        election = turnout_vic.VicElection(
            '2018vic',
            '2018-11-24',
            'https://example.test/state2018/summary.html',
            1,
            'historical',
            ('Example District',),
        )
        url = (
            'https://example.test/state2018/'
            'fpvbyvotingcentreexampledistrict.html'
        )
        page = district_html(
            year='2018', historical=True, recount=True
        )
        for category in (
            b'Ordinary Votes Total', b'Absent Votes', b'Early Votes',
            b'Marked As Voted Votes', b'Postal Votes', b'Provisional Votes',
        ):
            row_start = page.index(b'<tr><td>' + category)
            row_end = page.index(b'</tr>', row_start) + len(b'</tr>')
            row = page[row_start:row_end]
            page = page.replace(
                row,
                b'<tr><td>' + category
                + b'</td><td>0</td><td>0</td><td>0</td><td>0</td></tr>',
            )

        dataset = turnout_vic.build_dataset(election, {url: page})

        self.assertEqual(dataset.seat_totals[0].total_ballots, 135)
        self.assertEqual(dataset.vote_types, [])

    def test_rejects_category_total_disagreement(self):
        page = district_html().replace(
            b'<td>Postal Votes</td><td>8</td><td>4</td><td>0</td><td>12</td>',
            b'<td>Postal Votes</td><td>7</td><td>4</td><td>0</td><td>11</td>',
        )

        with self.assertRaises(turnout_data.TurnoutDataError):
            turnout_vic.build_dataset(
                self.current_election, {self.current_url: page}
            )


if __name__ == '__main__':
    unittest.main()
