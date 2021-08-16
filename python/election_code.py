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

    def __repr__(self):
        return f'ElectionCode({self.year()}, {self.region()})'

    @staticmethod
    def load_elections_from_file(file):
        split_lines = [line.strip().split(',') for line in file.readlines()]
        return [ElectionCode(int(a[0]), a[1]) for a in split_lines]