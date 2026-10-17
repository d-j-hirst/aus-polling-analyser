"""Retain public source responses for the historical category-TPP diagnostic.

Downloads are opt-in. Cached bytes and their URL/hash manifest make subsequent
analysis repeatable without contacting a commission or altering live inputs.
"""

import hashlib
import json
from pathlib import Path
from urllib.request import Request, urlopen

ROOT = Path(__file__).resolve().parents[3]
CACHE = ROOT / 'downloads' / 'category-tpp-research'


def fetch(url, download=False):
    key = hashlib.sha256(url.encode()).hexdigest()
    path = CACHE / key
    if path.exists():
        return path.read_bytes()
    if not download:
        raise FileNotFoundError(f'Uncached source: {url}')
    request = Request(url, headers={'User-Agent':
        'AEF election research (https://www.aeforecasts.com/; aeforecasts@gmail.com)'})
    with urlopen(request, timeout=30) as response:
        data = response.read()
        final_url = response.url
    CACHE.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)
    path.with_suffix('.json').write_text(json.dumps({
        'url': url, 'resolved_url': final_url,
        'sha256': hashlib.sha256(data).hexdigest(), 'bytes': len(data),
    }, indent=2), encoding='utf-8')
    return data
