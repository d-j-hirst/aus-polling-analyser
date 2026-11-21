"""Render the commission's vote-category names in published commentary.

Counts, model checks and links to individual evidence records use stable
internal categories. Both writers' verified text passes through this display
step when a package is released. Each election can supply the labels from
its own feed, including distinctions between different kinds of early vote.
"""
from pathlib import Path
import re

from .storage import read


class VoteTypeNames:
    def __init__(self, election):
        self.names = {}
        if not re.fullmatch(r'[A-Za-z0-9_-]+', election):
            return
        path = Path(__file__).resolve().parents[2] / 'forecasts' / election / 'commentary-vote-types.json'
        if not path.is_file():
            return
        labels = read(path)
        if not isinstance(labels, dict) or any(not key.strip() or not isinstance(value, str)
                                               or not value.strip() for key, value in labels.items()):
            raise ValueError(f'Expected a mapping of vote types to public names in {path}')
        self.names = {code.casefold(): label for code, label in labels.items()}
        if not self.names:
            return
        codes = '|'.join(re.escape(code) for code in sorted(self.names, key=len, reverse=True))
        if 'prepoll' in self.names:
            codes = codes.replace('prepoll', r'pre[- ]?poll')
        # Standalone machine abbreviations also turn up as subjects, e.g.
        # “EVM reported first preferences”. Ordinary words such as “absent”
        # require counting context. A named pre-poll centre is a physical booth.
        identifiers = [code for code in labels if ' ' not in code and any(c.isupper() for c in code[1:])]
        bare = ''
        if identifiers:
            names = '|'.join(map(re.escape, identifiers))
            bare = r'|\b(?P<bare>(?-i:'+names+r'))\b(?![- ](?:centre|center|booth)\b)'
        official = '|'.join(re.escape(label) for label in sorted(labels.values(), key=len, reverse=True))
        # Match grammatical uses of a category, keeping a word like “absent”
        # in “absent from the feed” intact. Already public names, entity
        # placeholders and the divider between editions are copied verbatim.
        self.phrases = re.compile(
            r'\{[^{}]*\}|\[\[[^\]]+\]\]|\b(?:'+official+r')\b|'
            r'\b(?P<code>'+codes+r')\b(?:\s+(?P<noun>counts?|ballots?|votes?)\b|'
            r'(?=\s*(?:[,.;:]|$|\band\b|\bto\b)))'+bare, re.I)

    def text(self, prose):
        if not self.names:
            return prose

        def public_phrase(match):
            bare = match.groupdict().get('bare')
            if bare:
                return self.names[bare.casefold()]
            if not match['code']:
                return match[0]
            key = match['code'].casefold()
            if key in ('pre-poll', 'pre poll'):
                key = 'prepoll'
            name = self.names[key]
            noun = (match['noun'] or '').lower()
            if noun.startswith('count'):
                return noun+' of '+name
            if noun in ('vote', 'ballot', 'ballots') and name.endswith(' Votes'):
                return name[:-6]+' '+noun
            if not noun and match['code'].islower():
                return match[0]  # For example, “TCP was absent.”
            return name

        return self.phrases.sub(public_phrase, prose)

    def reports(self, results):
        """Preserve receipt status, candidate badges and historical packages.
        Only the prose of the publication copy receives the display labels."""
        if not self.names:
            return results
        public = dict(results)
        for window, report in results.items():
            contents = report.get('text')
            if not contents:
                continue
            sections = [dict(section, text=self.text(section['text'])) for section in contents['developments']]
            public_text = dict(contents, developments=sections)
            # A timed-out AI response can still have a rules edition, with
            # its headline below the divider and no primary summary.
            if 'summary' in contents:
                public_text['summary'] = self.text(contents['summary'])
            public[window] = dict(report, text=public_text)
        return public
