import argparse
import datetime
import environ
import json
import math
import os
import requests

# Folder watching adds reporting and immutable queue publication. The existing
# one-off commands and their destination selection remain available below.

def handle_response(response):
    decoded = response.content.decode()
    try:
        response_data = response.json()
    except ValueError:
        response_data = None
    # Rebuilds now return counts, while report and older-server responses keep
    # using the existing generic response handling below.
    if isinstance(response_data, dict) and 'rebuilt' in response_data:
        rebuilt = ', '.join(
            f'{mode}: {count}'
            for mode, count in response_data['rebuilt'].items()
        )
        print(f"Rebuilt {response_data['code']} timeseries ({rebuilt})")
        return
    if len(decoded) < 200:
        print(decoded)
    else:
        decoded = decoded.replace('\\n', '\n')
        decoded = decoded.replace('"', '')
        with open('long_response.txt', mode='w', encoding='utf-8') as f:
            f.write(decoded)
        print('Response too long, written to file long_response.txt')

parser = argparse.ArgumentParser(description='Publish forecasts and manage live or replay runs.')
parser.add_argument('--local', action='store_true',
                    help='Upload to local server only (default)')
parser.add_argument('--test', action='store_true',
                    help='Upload to test server only')
parser.add_argument('--remote', action='store_true',
                    help='Upload to remote server only')
parser.add_argument('--all', action='store_true',
                    help='Upload to local, test and production servers')
parser.add_argument('--commentary', metavar='FILE', help='Publish a human-written UTF-8 article to the Commentary page')
parser.add_argument('--article-key', help='Stable identity to create or replace the same article on each server')
parser.add_argument('--title', help='Article title (required for a new article)')
parser.add_argument('--existing-title', help='Attach the key to an older article by its exact, unique title')
tag_options = parser.add_mutually_exclusive_group()
tag_options.add_argument('--tag', action='append', help='Set article tags (repeat for each tag)')
tag_options.add_argument('--clear-tags', action='store_true', help='Remove all tags from this article')
parser.add_argument('--watch', metavar='RUN_ID', help='Watch a saved live/replay run by ID or explicit directory')
parser.add_argument('--idle-hours', type=float, metavar='HOURS',
                    help='With --watch, exit after this many hours without a new forecast (default 14, 0 disables)')
parser.add_argument('--complete-replay', metavar='RUN_ID',
                    help='Mark a saved replay complete on the selected servers after checking all published snapshots')
parser.add_argument('--commentary-config', help='Override the run’s saved commentary settings with this JSON file')
parser.add_argument('--once', action='store_true', help='Process currently ready packages, then exit')
parser.add_argument('--prepare-only', action='store_true', help='Prepare packages without uploading (commentary only with an explicit override)')
parser.add_argument('--timeseries', action='store',
                    help='Update timeseries instead of uploading forecast. '
                         'Include argument e.g. 2022fed')
parser.add_argument('--timeseries-mode', choices=['FC', 'NC', 'LF', 'all'],
                    default='all',
                    # Reconstruction addresses stored history modes, where a
                    # regular forecast is FC. Report JSON continues to use RF.
                    help='Forecast mode to rebuild when using --timeseries '
                         '(default: all)')
parser.add_argument('--results', action='store',
                    help='Update results instead of uploading forecast. '
                         'Include argument e.g. 2022fed')
parser.add_argument('--review', action='store',
                    help='Review results instead of uploading forecast. '
                         'Include argument e.g. 2022fed')
parser.add_argument('--clearcache', action='store_true',
                    help='Clear cache. Useful if an incorrect forecast '
                         'has been uploaded.')
args = parser.parse_args()
if not args.commentary and any((args.article_key, args.title, args.existing_title, args.tag, args.clear_tags)):
    parser.error('Article options require --commentary FILE.')
if args.idle_hours is not None and (not args.watch or not math.isfinite(args.idle_hours) or args.idle_hours < 0):
    parser.error('--idle-hours requires --watch and a finite number of hours of zero or more.')
upload_local = args.local or args.all
upload_test = args.test or args.all
upload_remote = args.remote or args.all
timeseries = args.timeseries
timeseries_mode = args.timeseries_mode
results = args.results
review = args.review
clear_cache = args.clearcache
if not (upload_local or upload_test or upload_remote):
    upload_local = True

env = environ.Env()
BASE_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
environ.Env.read_env(os.path.join(BASE_DIR, 'uploads/.env'))
if args.commentary:
    if any((args.watch, args.complete_replay, args.prepare_only, args.once, args.commentary_config,
            timeseries, results, review, clear_cache, args.timeseries_mode != 'all')):
        parser.error('--commentary is a separate command. Use its article and destination options.')
    from live_pipeline.articles import article_payload, publish_article
    try:
        article = article_payload(args.commentary, args.article_key, args.title,
                                  [] if args.clear_tags else args.tag, args.existing_title)
    except (OSError, ValueError) as error:
        parser.error(str(error))
    selected = [name for name, active in (('local', upload_local), ('test', upload_test), ('remote', upload_remote)) if active]
    raise SystemExit(0 if publish_article(args.article_key, article, selected, env('AUTO_EMAIL'), env('AUTO_PASSWORD')) else 1)
if args.complete_replay:
    if any((args.watch, args.prepare_only, args.once, args.commentary_config, timeseries, results, review, clear_cache)):
        parser.error('--complete-replay is a separate command; use it with the destination flags.')
    from live_pipeline.protocol import DESTINATIONS, Server, replay_manifest
    from live_pipeline.run_settings import check_trial_options, load_run
    selected = [name for name, active in (('local', upload_local), ('test', upload_test), ('remote', upload_remote)) if active]
    try:
        folder, config = load_run(BASE_DIR, args.complete_replay, allow_path=True)
        check_trial_options(config, selected)
        publications = replay_manifest(folder, config)
    except (OSError, ValueError) as error:
        parser.error(str(error))
    failed = False
    for name in selected:
        try:
            receipt = Server(DESTINATIONS[name], env('AUTO_EMAIL'), env('AUTO_PASSWORD')).complete_replay(config, publications)
            print(f"{name}: replay {config['run_id']} complete ({receipt['snapshot_count']} snapshots).", flush=True)
        except (requests.RequestException, ValueError) as error:
            print(f"{name}: could not mark {config['run_id']} complete: {error}", flush=True)
            failed = True
    raise SystemExit(int(failed))
if args.watch:
    from live_pipeline.run_settings import load_run
    from live_pipeline.storage import RunLockBusy
    from live_pipeline.watch import DEFAULT_IDLE_HOURS, run
    try:
        folder, _ = load_run(BASE_DIR, args.watch, allow_path=True)
    except (OSError, ValueError) as error:
        parser.error(str(error))
    destinations = []
    if not args.prepare_only:
        destinations = [name for name, active in (
            ('local', upload_local), ('test', upload_test), ('remote', upload_remote)
        ) if active]
    try:
        run(folder, destinations, args.commentary_config, args.once, args.prepare_only,
            DEFAULT_IDLE_HOURS if args.idle_hours is None else args.idle_hours)
    except (RunLockBusy, ValueError) as error:
        raise SystemExit(str(error))
    raise SystemExit(0)
AUTO_EMAIL = env('AUTO_EMAIL')
AUTO_PASSWORD = env('AUTO_PASSWORD')

print(f'Upload initiated: {datetime.datetime.now()}')

if timeseries:
    data = json.dumps({
        'termCode': timeseries,
        'mode': timeseries_mode,
    })
    url_part = 'submit-timeseries-update'
elif results:
    url_part = 'submit-results-update'
    pre_fill = None
    if not upload_local:
        with open("prefill_results.json", encoding='utf-8') as f:
            pre_fill = json.load(f)
        if pre_fill.get("code") != results:
            pre_fill = None
    data = '{"termCode":"' + results + '", "preFill":' + json.dumps(pre_fill) + '}'
    print(data)
elif review:
    data = '{"termCode":"' + review + '"}'
    url_part = 'submit-review'
elif clear_cache:
    review = '2022fed'
    data = '{"termCode":"' + review + '"}'
    url_part = 'reset-cache'
else:
    with open("latest_json.dat", encoding='utf-8') as f:
        data = f.read()
        print(f"Report name: {json.loads(data)['reportLabel']}")
        print(f"Report mode: {json.loads(data)['reportMode']}")
        url_part = 'submit-report'

login_data = {'email': AUTO_EMAIL, 'password': AUTO_PASSWORD}
encoded_data = data.encode('utf-8')


def authenticated_json_headers(token):
    return {
        'Authorization': 'JWT ' + token,
        'Content-Type': 'application/json; charset=utf-8',
    }

if upload_local:
    print("Sending to local server:")
    response = requests.post('http://localhost:8000/auth-api/v1/auth/login/', data=login_data)
    token = response.cookies['jwt_token']

    headers = authenticated_json_headers(token)

    response = requests.post(f'http://localhost:8000/forecast-api/{url_part}', headers=headers, data=encoded_data)
    handle_response(response)

if upload_test:
    print("Sending to remote test server:")
    response = requests.post('https://dendrite.pythonanywhere.com/auth-api/v1/auth/login/', data=login_data)

    token = response.cookies['jwt_token']

    headers = authenticated_json_headers(token)

    response = requests.post(f'https://dendrite.pythonanywhere.com/forecast-api/{url_part}', headers=headers, data=encoded_data)
    handle_response(response)

if upload_remote:
    print("Sending to remote server:")
    response = requests.post('https://www.aeforecasts.com/auth-api/v1/auth/login/', data=login_data)

    token = response.cookies['jwt_token']

    headers = authenticated_json_headers(token)

    response = requests.post(f'https://www.aeforecasts.com/forecast-api/{url_part}', headers=headers, data=encoded_data)
    handle_response(response)
    
print(f'Upload completed: {datetime.datetime.now()}')
