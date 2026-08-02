"""Load and validate the authored inputs used by trend adjustment.

Parent: trend_adjust.py coordinates the overall adjustment workflow and
passes these validated inputs to fundamentals and mixing stages.

Main functions:
* ``PartyGroupConfig`` and ``ElectionPartyCode`` normalize authored party
  group settings and output filenames.
* ``Inputs`` loads, validates and exposes all historical context consumed by
  the fundamentals and mixing calculations.
* ``PollTrend`` loads historical cutoff/poll trend series and supplies the
  point-in-time values used in adjustment fitting.
* ``adjustment_reference_year`` and ``prior_result_average`` implement small
  shared historical-selection rules.
"""

from dataclasses import dataclass
from datetime import date
import math
from types import MappingProxyType

from numpy import median

from election_code import ElectionCode, no_target_election_marker
from trend_adjust_cutoffs import CutoffTrendData


UNNAMED_OTHERS_BASE = 3
TREND_ADJUSTMENT_LEVELS = (-100, -80, -60, -40, -20, 0)
ADJUSTMENT_PARAMETER_COUNT = 8
PARTIES_OUTSIDE_OTHERS = {
    '@TPP', 'ALP FP', 'LNP FP', 'LIB FP', 'NAT FP', 'GRN FP', 'OTH FP'
}


@dataclass(frozen=True)
class PartyGroupConfig:
    """Party categories shared by all trend-adjustment stages."""

    groups: object
    average_lengths: object
    unnamed_others_code: str

    @classmethod
    def load(cls, filename='./Data/party-groups.csv'):
        with open(filename, 'r', encoding='utf-8') as input_file:
            rows = [line.strip().split(',') for line in input_file if line.strip()]
        groups = {row[0]: tuple(row[1:]) for row in rows}
        if 'xOTH' not in groups or not groups['xOTH']:
            raise TrendAdjustmentDataError(
                f'{filename} must define an xOTH party group'
            )
        average_lengths = {
            group: 6 if group in ('ALP', 'LNP', 'TPP') else 1
            for group in groups
        }
        return cls(
            groups=MappingProxyType(groups),
            average_lengths=MappingProxyType(average_lengths),
            unnamed_others_code=groups['xOTH'][0],
        )


class ElectionPartyCode:
    """Hashable key for one party at one election."""

    def __init__(self, election, party):
        self._internal = (int(election.year()),
                          str(election.region()),
                          str(party))

    def __hash__(self):
        return hash((self._internal))

    def __eq__(self, another):
        if not isinstance(another, ElectionPartyCode):
            return NotImplemented
        return self._internal == another._internal

    def year(self):
        return self._internal[0]

    def region(self):
        return self._internal[1]

    def party(self):
        return self._internal[2]

    def __repr__(self):
        return (f'ElectionPartyCode({self.year()}, '
                f'{self.region()}, {self.party()})')


class TrendAdjustmentDataError(ValueError):
    """Raised when authored inputs cannot produce valid adjustments."""


# Shared date and prior-result helpers

def adjustment_reference_year(excluded_election, current_year=None):
    """Return the year against which historical poll errors are weighted.

    Election-specific hindcasts use the target election year. The generic
    ``0none`` adjustment is for forecasts made now, so treating its marker year
    of zero literally would make the oldest elections appear most relevant.
    """

    if excluded_election != no_target_election_marker:
        return excluded_election.year()
    return date.today().year if current_year is None else current_year


def prior_result_average(values, requested_count):
    """Apply the established median/trimmed-mean prior robustly."""

    selected = values[:requested_count]
    if not selected:
        raise TrendAdjustmentDataError(
            'prior-result history cannot be empty'
        )
    if requested_count < 5 or len(selected) < 5:
        return median(selected)
    trimmed = sorted(selected)[1:-1]
    return sum(trimmed) / len(trimmed)


# Authored-input loading and validation

class Inputs:
    """Authored election inputs used by both adjustment calculations."""

    def __init__(self, exclude, party_groups):
        self.party_groups = party_groups
        # Only elections with usable polling data
        # [0] year of election, [1] region of election
        with open('./Data/polled-elections.csv', 'r') as f:
            self.polled_elections = ElectionCode.load_elections_from_file(f, exclude=exclude)
        # Old elections without enough polling data, but still useful
        # for determining fundamentals forecasts
        # [0] year of election, [1] region of election
        with open('./Data/old-elections.csv', 'r') as f:
            old_elections = ElectionCode.load_elections_from_file(f, exclude=exclude)
        old_elections = [a for a in old_elections if a != exclude]
        self.past_elections = self.polled_elections + old_elections
        # We need data for the current election
        self.all_elections = self.past_elections + [exclude]
        # key: [0] year of election, [1] region of election
        # value: list of significant party codes modelled in that election
        with open('./Data/significant-parties.csv', 'r') as f:
            parties = {
                ElectionCode(a[0], a[1]): a[2:]
                for a in [b.strip().split(',') for b in f.readlines()]
                if ElectionCode(a[0], a[1]) in self.all_elections
            }
        # key: [0] year of election, [1] region of election, [2] party code
        # value: primary vote recorded in this election
        # avoid storing eventual results for current election since that shouldn't
        # be used for predicting it
        with open('./Data/eventual-results.csv', 'r') as f:
            self.eventual_results = {
                ElectionPartyCode(ElectionCode(a[0], a[1]), a[2]): float(a[3])
                for a in [b.strip().split(',') for b in f.readlines()]
                if ElectionCode(a[0], a[1]) in self.past_elections
            }
        # key: [0] year of election, [1] region of election, [2] party code
        # value: primary vote recorded in the previous election
        with open('./Data/prior-results.csv', 'r') as f:
            linelists = [b.strip().split(',') for b in f.readlines()]
            self.prior_results = {
                ElectionPartyCode(ElectionCode(a[0], a[1]), a[2]):
                [float(x) for x in a[3:]]
                for a in linelists
                if ElectionCode(a[0], a[1]) in self.all_elections
            }

        # stores: first incumbent party, then main opposition party,
        # finally years incumbent party has been in power
        with open('./Data/incumbency.csv', 'r') as f:
            self.incumbency = {
                ElectionCode(a[0], a[1]): (a[2], a[3], float(a[4]))
                for a in [b.strip().split(',') for b in f.readlines()]
                if ElectionCode(a[0], a[1]) in self.all_elections
            }
        # stores: party corresponding to federal government,
        # then party opposing federal government,
        # then the chance the federal government is still in power at this election,
        # given in the file as a percentage (defaults to 100 if not given)
        with open('./Data/federal-situation.csv', 'r') as f:
            self.federal_situation = {
                ElectionCode(a[0], a[1]):
                (a[2], a[3], float(a[4]) / 100 if len(a) >= 5 else 1)
                for a in [b.strip().split(',') for b in f.readlines()]
                if ElectionCode(a[0], a[1]) in self.all_elections
            }
        
        # stores: preference flow to ALP, then exhaust rate (0 if not given)
        with open('./Data/preference-estimates.csv', 'r') as f:
            self.preference_estimates = {
                ElectionPartyCode(ElectionCode(a[0], a[1]), a[2]):
                (float(a[3]), float(a[4]) if len(a) >= 5 else 0)
                for a in [b.strip().split(',') for b in f.readlines()]
                if ElectionCode(a[0], a[1]) in self.all_elections
            }
        invalid_preferences = [
            party_code
            for party_code, (flow, exhaust) in self.preference_estimates.items()
            if (not math.isfinite(flow)
                or not math.isfinite(exhaust)
                or not 0 < flow < 100
                or not 0 <= exhaust < 100)
        ]
        if invalid_preferences:
            raise TrendAdjustmentDataError(
                'Preference estimates require flow strictly between 0 and '
                '100 and exhaust from 0 up to (but not including) 100: '
                + ', '.join(map(str, invalid_preferences))
            )

        # Keep independent lists. The old shared-list setup meant that adding
        # the derived xOTH category through one view silently added it through
        # every other view as well.
        self.polled_parties = {
            election: list(parties[election])
            for election in self.polled_elections
        }
        self.past_parties = {
            election: list(parties[election])
            for election in self.past_elections
        }
        self.all_parties = {
            election: list(parties[election])
            for election in self.all_elections
        }
        # Create averages of prior results
        avg_counts = list(range(1, 9))
        self.avg_prior_results = {
            avg_n: {
                key: prior_result_average(values, avg_n)
                for key, values in self.prior_results.items()
            } for avg_n in avg_counts}
        self.studied_elections = self.polled_elections + [no_target_election_marker]
        self.fundamentals = {}  # Filled in later
        self.exclude = exclude
        self.reference_year = adjustment_reference_year(exclude)
    
    def safe_prior_average(self, n_elections, e_p_c):
        if n_elections not in self.avg_prior_results:
            n_elections = 1
        if e_p_c in self.avg_prior_results[n_elections]:
            return self.avg_prior_results[n_elections][e_p_c]
        else:
            return 0
        

    def determine_eventual_others_results(self):
        """Derive residual Others results not assigned to named parties."""

        for e in self.past_elections:
            others_code = ElectionPartyCode(e, 'OTH FP')
            eventual_others = self.eventual_results[others_code]
            eventual_named = 0
            for p in self.past_parties[e]:
                party_code = ElectionPartyCode(e, p)
                if (p not in PARTIES_OUTSIDE_OTHERS
                        and party_code in self.eventual_results):
                    eventual_named += self.eventual_results[party_code]
            eventual_unnamed = eventual_others - eventual_named
            if eventual_unnamed < 0:
                raise TrendAdjustmentDataError(
                    f'{e.short()} named minor-party results exceed its OTH '
                    f'result by {-eventual_unnamed:.4f} percentage points'
                )
            unnamed_code = ElectionPartyCode(e, self.party_groups.unnamed_others_code)
            self.eventual_results[unnamed_code] = eventual_unnamed


def parties_with_unnamed_others(parties, unnamed_others_code):
    """Return configured parties plus exactly one derived xOTH category."""

    return list(dict.fromkeys([*parties, unnamed_others_code]))


class PollTrend:
    """Historical cutoff trends accessed by election, party and day."""

    def __init__(self, inputs, config):
        self._party_groups = inputs.party_groups
        self._data = {}
        self._party_lists = {}
        self.cutoff_record_keys = []
        for election, party_list in inputs.polled_parties.items():
            cutoff_filename = (
                f'./Outputs/Cutoffs/cutoffs_{election.short()}.csv'
            )
            if config.show_loaded_files:
                print(cutoff_filename)
            data = CutoffTrendData(cutoff_filename)
            configured_parties = [
                party for party in party_list
                if party != self._party_groups.unnamed_others_code
            ]
            data.require_parties(configured_parties)
            election_key = election.pair()
            self._data[election_key] = data
            # xOTH is derived from inclusive OTH and named-party trends; it is
            # deliberately absent from the source cutoff file.
            self._party_lists[election_key] = tuple(party_list)
            self.cutoff_record_keys.append(
                "cutoff_poll_outputs:{}".format(election.short())
            )

    def value_at(self, party_code, day, percentile, default_value=None):
        election_key = (party_code.year(), party_code.region())
        if party_code.party() == self._party_groups.unnamed_others_code:
            return self.exclusive_others_value_at(
                election_key, day, percentile, default_value
            )
        return self._data[election_key].value_at(
            party_code.party(), day, percentile, default_value
        )

    def exclusive_others_value_at(
        self, election_key, day, percentile, default_value
    ):
        """Derive unnamed Others after interpolating its components."""

        data = self._data[election_key]
        party_list = self._party_lists[election_key]
        # Base of 3% for unnamed others mirrors the C++ code
        named_medians = [
            data.value_at(party, day, 50, default_value)
            for party in party_list
            if party not in PARTIES_OUTSIDE_OTHERS
        ]
        oth_median = data.value_at(
            'OTH FP', day, 50, default_value
        )
        oth_value = data.value_at(
            'OTH FP', day, percentile, default_value
        )
        if (
            oth_median is None
            or oth_value is None
            or any(value is None for value in named_medians)
        ):
            return default_value
        named_median = sum(named_medians)
        modified_oth_median = max(
            oth_median, named_median + UNNAMED_OTHERS_BASE
        )
        xoth_proportion = 1 - named_median / modified_oth_median
        return oth_value * xoth_proportion
