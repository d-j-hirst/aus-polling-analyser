"""Submit a human-written UTF-8 text file to the website's Commentary page."""
from pathlib import Path
import re

import requests

from .protocol import DESTINATIONS, Server


def article_payload(path, key, title=None, tags=None, existing_title=None):
    if not key or not re.fullmatch(r'[A-Za-z0-9_-]{1,100}', key):
        raise ValueError('--article-key must be 1 to 100 letters, digits, hyphens or underscores.')
    text = Path(path).read_text(encoding='utf-8-sig')
    if not text.strip() or '\x00' in text:
        raise ValueError('The commentary file must contain non-empty UTF-8 text.')
    data = {'text': text}
    for field, value in (('title', title), ('existing_title', existing_title)):
        if value is not None:
            if not value.strip() or '\x00' in value:
                raise ValueError(f'{field} cannot be blank or contain a null character.')
            data[field] = value.strip()
    if tags is not None:
        if any(not tag.strip() or len(tag.strip()) > 64 or '\x00' in tag for tag in tags):
            raise ValueError('Each tag must contain 1 to 64 characters.')
        data['tags'] = [tag.strip() for tag in tags]
    return data


def publish_article(key, data, destinations, email, password):
    """Try each destination independently. A retry with the same key keeps the article URL."""
    success = True
    for name in destinations:
        server = Server(DESTINATIONS[name], email, password)
        try:
            server.authenticate()
            response = server.session.put(f'{server.base}/commentary-api/publish/{key}/',
                                          json=data, timeout=30)
            if response.status_code == 404:
                raise ValueError('The human commentary endpoint is unavailable at this destination.')
            if response.status_code == 409:
                raise ValueError(response.json().get('detail', 'Article identity conflict.'))
            response.raise_for_status()
            receipt = response.json()
            if (not isinstance(receipt, dict) or receipt.get('publication_key') != key or
                    type(receipt.get('id')) is not int or receipt['id'] <= 0):
                raise ValueError('The server did not acknowledge the requested article.')
            print(f"{name}: published {key} at /commentary/{receipt['id']}/.", flush=True)
        except (requests.RequestException, ValueError) as error:
            print(f'{name}: could not publish {key}: {error}', flush=True)
            success = False
        finally:
            server.session.close()
    return success
