import unittest

import turnout_aec
import turnout_data


def csv_bytes(metadata_name, header, rows):
    lines = [
        '{} [Event:999 Phase:FinalResults Generated:2026-01-01]'.format(
            metadata_name
        ),
        ','.join(header),
    ]
    lines.extend(','.join(str(value) for value in row) for row in rows)
    return ('\n'.join(lines) + '\n').encode('utf-8')


class AecTurnoutAdapterTests(unittest.TestCase):
    election = turnout_aec.AecElection('2025fed', '2025-05-03', 999)

    def files(self):
        vote_columns = list(turnout_aec.VOTE_COLUMNS)
        return {
            'candidate_vote_types': csv_bytes(
                'Candidate vote types',
                [
                    'StateAb', 'DivisionID', 'DivisionNm', 'CandidateID',
                    'Surname', 'PartyNm', *vote_columns, 'TotalVotes',
                ],
                [
                    ['SA', 1, 'Example', 10, 'Candidate', 'Labor', 60, 5, 1, 2, 12, 80],
                    ['SA', 1, 'Example', 20, 'Other', 'Liberal', 30, 3, 0, 1, 6, 40],
                    ['SA', 1, 'Example', 999, 'Informal', 'Informal', 5, 1, 0, 0, 4, 10],
                ],
            ),
            'enrolment': csv_bytes(
                'Enrolment',
                ['DivisionID', 'DivisionNm', 'StateAb', 'Enrolment'],
                [[1, 'Example', 'SA', 150]],
            ),
            'informal': csv_bytes(
                'Informal',
                [
                    'DivisionID', 'DivisionNm', 'StateAb', 'FormalVotes',
                    'InformalVotes', 'TotalVotes',
                ],
                [[1, 'Example', 'SA', 120, 10, 130]],
            ),
            'division_votes': csv_bytes(
                'Division votes',
                [
                    'DivisionID', 'DivisionNm', 'StateAb', 'Enrolment',
                    *vote_columns, 'TotalVotes',
                ],
                [[1, 'Example', 'SA', 150, 95, 9, 1, 3, 22, 130]],
            ),
        }

    def test_builds_reconciled_vote_type_records(self):
        dataset = turnout_aec.build_dataset(self.election, self.files())

        self.assertEqual(len(dataset.seat_totals), 1)
        self.assertEqual(len(dataset.vote_types), 5)
        self.assertEqual(dataset.seat_totals[0].subdivision, 'sa')
        ordinary = dataset.vote_types[0]
        self.assertEqual(ordinary.canonical_category, 'ordinary_combined')
        self.assertEqual(ordinary.formal_votes, 90)
        self.assertEqual(ordinary.informal_votes, 5)
        self.assertEqual(ordinary.total_ballots, 95)

    def test_rejects_category_total_disagreement(self):
        files = self.files()
        files['division_votes'] = files['division_votes'].replace(
            b',95,9,1,3,22,130',
            b',94,9,1,3,22,129',
        )

        with self.assertRaisesRegex(
            turnout_data.TurnoutDataError,
            'total differs between AEC files',
        ):
            turnout_aec.build_dataset(self.election, files)

    def test_rejects_non_final_input(self):
        files = self.files()
        files['informal'] = files['informal'].replace(
            b'Phase:FinalResults', b'Phase:Preliminary'
        )

        with self.assertRaisesRegex(
            turnout_data.TurnoutDataError,
            'not a final-results file',
        ):
            turnout_aec.build_dataset(self.election, files)

    def test_pre_2010_ordinary_votes_are_election_day_votes(self):
        election = turnout_aec.AecElection(
            '2007fed',
            '2007-11-24',
            999,
            ordinary_category='election_day_ordinary',
        )

        dataset = turnout_aec.build_dataset(election, self.files())

        self.assertEqual(
            dataset.vote_types[0].canonical_category,
            'election_day_ordinary',
        )

    def test_legacy_election_uses_votes_file_enrolment(self):
        files = self.files()
        del files['enrolment']
        for file_kind in files:
            files[file_kind] = files[file_kind].replace(
                b'Candidate vote types [Event:999 Phase:FinalResults Generated:2026-01-01]',
                b'2004 Federal Election Candidate vote types [999 8/11/2005]',
            ).replace(
                b'Informal [Event:999 Phase:FinalResults Generated:2026-01-01]',
                b'2004 Federal Election Informal [999 8/11/2005]',
            ).replace(
                b'Division votes [Event:999 Phase:FinalResults Generated:2026-01-01]',
                b'2004 Federal Election Division votes [999 8/11/2005]',
            )
        election = turnout_aec.AecElection(
            '2004fed',
            '2004-10-09',
            999,
            ordinary_category='election_day_ordinary',
            download_path='results/Downloads',
            has_enrolment_file=False,
            legacy_metadata=True,
        )

        dataset = turnout_aec.build_dataset(election, files)

        self.assertEqual(dataset.seat_totals[0].enrolment, 150)

    def test_legacy_download_path_is_used(self):
        election = turnout_aec.ELECTIONS['2004fed']
        self.assertIn(
            '/12246/results/Downloads/',
            turnout_aec._file_url(election, 'candidate_vote_types'),
        )


if __name__ == '__main__':
    unittest.main()
