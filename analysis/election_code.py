class ElectionCode:
    def __init__(self, year, region):
        self._internal = (int(year), str(region))

    def __hash__(self):
        return hash(self._internal)

    def __eq__(self, another):
        return self._internal == another._internal

    def year(self):
        return self._internal[0]

    def region(self):
        return self._internal[1]

    def pair(self):
        return (self.year(), self.region())

    def __repr__(self):
        return f'ElectionCode({self.year()}, {self.region()})'
    
    def short(self):
        return f'{self.year()}{self.region()}'

    @staticmethod
    def load_elections_from_file(file, exclude=None):
        split_lines = [line.strip().split(',') for line in file.readlines()]
        codes = [ElectionCode(int(a[0]), a[1]) for a in split_lines]
        if exclude is not None and exclude in codes:
            codes = codes[:codes.index(exclude)]
        return codes


no_target_election_marker = ElectionCode(0, 'none')