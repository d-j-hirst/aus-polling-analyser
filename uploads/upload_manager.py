import argparse
import datetime
import environ
import json
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

parser = argparse.ArgumentParser(
    description='Determine trend adjustment parameters')
parser.add_argument('--local', action='store_true',
                    help='Upload to local server only (default)')
parser.add_argument('--test', action='store_true',
                    help='Upload to test server only')
parser.add_argument('--remote', action='store_true',
                    help='Upload to remote server only')
parser.add_argument('--all', action='store_true',
                    help='Upload to local, test and production servers')
parser.add_argument('--watch', metavar='RUN_ID', help='Watch a saved live/replay run by ID or explicit directory')
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
if args.watch:
    from live_pipeline.run_settings import load_run
    from live_pipeline.storage import RunLockBusy
    from live_pipeline.watch import run
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
        run(folder, destinations, args.commentary_config, args.once, args.prepare_only)
    except RunLockBusy:
        raise SystemExit(f'A reporting worker is already running for {folder.name}. '
                         'Stop the existing worker before restarting the upload manager.')
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
