"""Check manual article submission with local files and simulated destination responses."""
from contextlib import redirect_stdout
from io import StringIO
from pathlib import Path
import sys
from tempfile import TemporaryDirectory
import unittest
from unittest.mock import Mock, patch

import requests

sys.path.insert(0, str(Path(__file__).resolve().parents[2]/'uploads'))
from live_pipeline import articles


class CommentaryUploadTests(unittest.TestCase):
    def test_plain_text_and_optional_metadata(self):
        with TemporaryDirectory() as folder:
            story = Path(folder)/'story.txt'
            story.write_bytes(b'\xef\xbb\xbfFirst paragraph.\r\n\r\nSecond paragraph.')
            self.assertEqual(articles.article_payload(story, 'vic-night'), {'text': 'First paragraph.\n\nSecond paragraph.'})
            self.assertEqual(articles.article_payload(story, 'vic-night', ' Heading ', [])['tags'], [])
            with self.assertRaises(ValueError):
                articles.article_payload(story, 'a/b')
            with self.assertRaises(ValueError):
                articles.article_payload(story, 'vic-night', tags=[' '])

    def test_destination_failure_does_not_stop_or_duplicate_another_article(self):
        def reply(key):
            response = Mock(status_code=200)
            response.json.return_value = {'publication_key': key, 'id': 7}
            return response

        with patch.object(articles, 'Server') as server, redirect_stdout(StringIO()):
            unavailable, accepted, wrong_article = Mock(), Mock(), Mock()
            unavailable.authenticate.side_effect = requests.ConnectionError('offline')
            accepted.base = 'https://test.example'
            accepted.session.put.return_value = reply('vic-night')
            wrong_article.session.put.return_value = reply('another-story')
            server.side_effect = [unavailable, accepted, wrong_article, accepted]
            data = {'text': 'New text'}
            self.assertFalse(articles.publish_article('vic-night', data, ['local', 'test'], 'user', 'password'))
            accepted.session.put.assert_called_once_with(
                'https://test.example/commentary-api/publish/vic-night/', json=data, timeout=30)
            self.assertFalse(articles.publish_article('vic-night', data, ['remote'], 'user', 'password'))
            self.assertTrue(articles.publish_article('vic-night', data, ['test'], 'user', 'password'))
            self.assertEqual([call.args[0] for call in server.call_args_list],
                             [articles.DESTINATIONS[k] for k in ('local', 'test', 'remote', 'test')])
