"""Check private replay setup and the boundary between federal predictors and SA outcomes."""
import json
import struct
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

import prepare_one_nation_booths as prep


class FederalBoothPreparationTests(unittest.TestCase):
    def test_replay_copies_model_with_own_sources_and_refuses_an_existing_run(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            controls = root / 'live_runs/2026sa-control'
            controls.mkdir(parents=True)
            previous = {'election': '2026sa', 'run_id': '2026sa-control', 'kind': 'replay'}
            model = b'model and simulation settings'
            project = (struct.pack('<i', 70) + prep.encoded('Original') + prep.encoded('2026 SA State Election')
                       + prep.encoded('previous macro') + model + prep.encoded(json.dumps(previous)))
            control = controls / 'control.pol2'
            control.write_bytes(project)
            archive = root / 'forecasts/2026sa/live-snapshots/feeds'
            archive.mkdir(parents=True)
            (archive / 'el2026260321183853.xml').write_bytes(b'zero capture')
            (root / 'live_scripts').mkdir()
            (root / 'live_scripts/Step-FederalBoothReplay.ps1').write_bytes(b'step')
            with patch.object(prep, 'ROOT', root):
                with self.assertRaises(FileExistsError):
                    prep.prepare_replay('2026sa-control',control,b'new predictor')
                added = prep.prepare_replay('2026sa-experiment',control,b'new predictor')
                with self.assertRaises(FileExistsError):
                    prep.prepare_replay('2026sa-experiment',control,b'different predictor')
            new = added.parents[1]
            settings = json.loads((new / 'run.json').read_bytes())
            self.assertTrue(settings['one_nation_booth_geography'])
            self.assertEqual(settings['source_directory'], 'live_runs/2026sa-experiment/inbox')
            self.assertEqual(list((new / 'inbox').iterdir()), [])
            self.assertEqual((new / 'captures.txt').read_text().splitlines(), ['20260321183853'])
            self.assertEqual(added.read_bytes(), b'new predictor')
            binary = (new / '2026sa-experiment.pol2').read_bytes()
            self.assertTrue(binary.endswith(model + prep.encoded(json.dumps(settings,separators=(',', ':')))))
            self.assertEqual(list(controls.iterdir()), [control])
            self.assertEqual(control.read_bytes(), project)
            self.assertFalse((root / 'forecasts/2026sa/live-inputs').exists())

    def test_changing_final_sa_booth_votes_leaves_the_location_predictor_identical(self):
        xml = b"""<root><district><district_name>Example</district_name><first_preferences>
        <candidate><polling_places><polling_place><polling_place_name>School</polling_place_name>
        <polling_place_id>45</polling_place_id></polling_place></polling_places></candidate>
        </first_preferences></district></root>"""
        def locations(outcome):
            table = ('mode,seat,booth,federal_area,three,nearest_km,on,formal\n'
                     f'ordinary,Example,School,Area,18/100,1.23,{outcome}\n').encode()
            with patch.object(prep, 'source', side_effect=[table,xml]):
                return prep.sa_places({}, {('Example','School')})
        self.assertEqual(locations('0,1000'),locations('999,1000'))
        places, areas = locations('50,1000')
        self.assertEqual(places['Example','School'], dict(id=45,one_nation=18,formal=100,nearest_km=1.23))
        self.assertEqual(areas, {'Example':'Area'})


if __name__ == '__main__':
    unittest.main()
