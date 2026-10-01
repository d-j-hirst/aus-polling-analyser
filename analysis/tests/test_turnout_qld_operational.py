from io import BytesIO
import unittest
from unittest import mock
import xml.etree.ElementTree as ET
from zipfile import ZipFile

import turnout_data
import turnout_qld_operational


SPREADSHEET_NAMESPACE = (
    'http://schemas.openxmlformats.org/spreadsheetml/2006/main'
)


def xlsx_bytes(rows):
    """Build the minimal shared-string XLSX subset used by adapter tests."""
    strings = []
    string_indexes = {}

    def cell(row_number, column, value):
        reference = '{}{}'.format(column, row_number)
        cell_node = ET.Element('c', {'r': reference})
        if isinstance(value, str):
            cell_node.set('t', 's')
            if value not in string_indexes:
                string_indexes[value] = len(strings)
                strings.append(value)
            stored_value = str(string_indexes[value])
        else:
            stored_value = str(value)
        ET.SubElement(cell_node, 'v').text = stored_value
        return cell_node

    worksheet = ET.Element('worksheet', {'xmlns': SPREADSHEET_NAMESPACE})
    sheet_data = ET.SubElement(worksheet, 'sheetData')
    for row_number, values in sorted(rows.items()):
        row = ET.SubElement(sheet_data, 'row', {'r': str(row_number)})
        for column, value in values.items():
            row.append(cell(row_number, column, value))

    shared = ET.Element(
        'sst',
        {
            'xmlns': SPREADSHEET_NAMESPACE,
            'count': str(len(strings)),
            'uniqueCount': str(len(strings)),
        },
    )
    for value in strings:
        item = ET.SubElement(shared, 'si')
        ET.SubElement(item, 't').text = value

    output = BytesIO()
    with ZipFile(output, 'w') as archive:
        archive.writestr(
            'xl/sharedStrings.xml',
            ET.tostring(shared, encoding='utf-8', xml_declaration=True),
        )
        archive.writestr(
            'xl/worksheets/sheet1.xml',
            ET.tostring(worksheet, encoding='utf-8', xml_declaration=True),
        )
    return output.getvalue()


class QldOperationalTurnoutAdapterTests(unittest.TestCase):
    election = turnout_qld_operational.ELECTIONS['2024qld']
    dates = (
        '14/10/2024', '15/10/2024', '16/10/2024', '17/10/2024',
        '18/10/2024', '21/10/2024', '22/10/2024', '23/10/2024',
        '24/10/2024', '25/10/2024', 45591,
    )

    def dataset(self):
        return turnout_data.TurnoutDataset(
            elections=[turnout_data.ElectionDefinition(
                '2024qld', '2024-10-26', 'qld'
            )],
            sources=[turnout_data.SourceDefinition(
                source_id='ecq-final',
                election_code='2024qld',
                authority='Electoral Commission of Queensland',
                locator='https://example.test/final',
                adapter='ecq-final-xml-v1',
                status='final',
                category_regime='ecq-current-vote-types-v1',
            )],
            seat_totals=[
                turnout_data.SeatTotal(
                    election_code='2024qld',
                    seat_name=name,
                    source_id='ecq-final',
                    enrolment=100,
                    formal_votes=None,
                    informal_votes=None,
                    total_ballots=None,
                    subdivision='qld',
                )
                for name in ('Alpha', 'Beta')
            ],
        )

    def attendance(self):
        columns = tuple(chr(code) for code in range(ord('B'), ord('M')))
        alpha = [1] * 11
        beta = [2] * 11
        totals = [left + right for left, right in zip(alpha, beta)]
        return xlsx_bytes({
            1: {'A': 'In-person voting attendance (mark-off)'},
            11: dict(
                [('A', 'State electorate')]
                + list(zip(columns, self.dates))
                + [('M', 'TOTAL')]
            ),
            12: dict(
                [('A', 'Total')]
                + list(zip(columns, totals))
                + [('M', sum(totals))]
            ),
            13: dict(
                [('A', 'Alpha')]
                + list(zip(columns, alpha))
                + [('M', sum(alpha))]
            ),
            14: dict(
                [('A', 'Beta')]
                + list(zip(columns, beta))
                + [('M', sum(beta))]
            ),
        })

    def postal(self, state_counts=(15, 11, 9)):
        return xlsx_bytes({
            1: {'A': 'Postal votes issued'},
            14: {'A': 'Current as at 5/11/2024'},
            16: {
                'A': 'State Electorate',
                'B': 'Postal ballots issued',
                'C': 'Postal ballots returned',
                'D': '% Postal ballots returned',
                'E': 'Postal votes accepted',
                'F': '% Postal votes accepted',
            },
            17: {
                'A': 'TOTAL', 'B': state_counts[0], 'C': state_counts[1],
                'E': state_counts[2],
            },
            18: {'A': 'Alpha', 'B': 10, 'C': 8, 'E': 7},
            19: {'A': 'Beta', 'B': 5, 'C': 3, 'E': 2},
        })

    def files(self, state_counts=(15, 11, 9)):
        return {
            'attendance': self.attendance(),
            'postal': self.postal(state_counts),
        }

    def test_builds_distinct_attendance_and_postal_geographies(self):
        dataset = turnout_qld_operational.merge_dataset(
            self.dataset(), self.election, self.files()
        )
        alpha_early = [
            record for record in dataset.operational_observations
            if record.seat_name == 'Alpha'
            and record.measure == turnout_qld_operational.EARLY_ATTENDANCE_MEASURE
        ]
        alpha_postal = [
            record for record in dataset.operational_observations
            if record.seat_name == 'Alpha'
            and record.measure == turnout_qld_operational.POSTAL_ISSUED_MEASURE
        ]
        self.assertEqual(len(alpha_early), 10)
        self.assertEqual(alpha_early[-1].count, 10)
        self.assertEqual(alpha_early[0].geography_basis, 'administering_division')
        self.assertEqual(alpha_postal[0].count, 10)
        self.assertEqual(alpha_postal[0].geography_basis, 'elector_division')

    def test_preserves_published_state_postal_totals_with_residuals(self):
        dataset = turnout_qld_operational.merge_dataset(
            self.dataset(), self.election, self.files((16, 12, 10))
        )
        state_records = [
            record for record in dataset.operational_observations
            if record.geography_basis == 'state'
        ]
        self.assertEqual(
            {record.measure: record.count for record in state_records},
            {
                turnout_qld_operational.POSTAL_ISSUED_MEASURE: 16,
                turnout_qld_operational.POSTAL_RETURNED_MEASURE: 12,
                turnout_qld_operational.POSTAL_ACCEPTED_MEASURE: 10,
            },
        )

    def test_rejects_state_postal_total_below_district_sum(self):
        with self.assertRaisesRegex(
            turnout_data.TurnoutDataError,
            'district postal totals .* exceed published state totals',
        ):
            turnout_qld_operational.merge_dataset(
                self.dataset(), self.election, self.files((14, 11, 9))
            )

    def test_rejects_inconsistent_district_postal_counts(self):
        files = self.files()
        rows = turnout_qld_operational.read_xlsx_rows(
            files['postal'], 'postal fixture'
        )
        rows[18]['C'] = 11
        files['postal'] = xlsx_bytes(rows)
        with self.assertRaisesRegex(
            turnout_data.TurnoutDataError,
            'Alpha postal accepted/returned/issued counts are inconsistent',
        ):
            turnout_qld_operational.merge_dataset(
                self.dataset(), self.election, files
            )

    def test_repeated_merge_replaces_adapter_records(self):
        dataset = turnout_qld_operational.merge_dataset(
            self.dataset(), self.election, self.files()
        )
        first_count = len(dataset.operational_observations)
        dataset = turnout_qld_operational.merge_dataset(
            dataset, self.election, self.files()
        )
        self.assertEqual(len(dataset.operational_observations), first_count)
        self.assertEqual(len(dataset.sources), 3)

    def test_download_uses_archived_copy_after_primary_failure(self):
        with mock.patch.object(
            turnout_qld_operational,
            '_download',
            side_effect=[
                turnout_qld_operational.URLError('blocked'), b'archive'
            ],
        ) as download:
            result = turnout_qld_operational._download_with_fallback(
                'https://example.test/official.xlsx',
                'https://archive.test/official.xlsx',
            )
        self.assertEqual(result, b'archive')
        self.assertEqual(download.call_count, 2)

    def test_download_rejects_non_workbook_response(self):
        response = mock.MagicMock()
        response.read.return_value = b'<!doctype html><title>Blocked</title>'
        response.__enter__.return_value = response
        with mock.patch.object(
            turnout_qld_operational, 'urlopen', return_value=response
        ):
            with self.assertRaisesRegex(
                turnout_data.TurnoutDataError,
                'did not return an XLSX workbook',
            ):
                turnout_qld_operational._download(
                    'https://example.test/workbook.xlsx'
                )


if __name__ == '__main__':
    unittest.main()
