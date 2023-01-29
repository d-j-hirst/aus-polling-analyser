import argparse
import datetime
import environ
import json
import os
import requests

def handle_response(response):
    decoded = response.content.decode()
    if len(decoded) < 200:
        print(decoded)
    else:
        decoded = decoded.replace('\\n', '\n')
        decoded = decoded.replace('"', '')
        with open('long_response.txt', mode='w') as f:
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
                    help='Upload to both local and remote server')
parser.add_argument('--timeseries', action='store',
                    help='Update timeseries instead of uploading forecast. '
                         'Include argument e.g. 2022fed')
parser.add_argument('--results', action='store',
                    help='Update results instead of uploading forecast. '
                         'Include argument e.g. 2022fed')
parser.add_argument('--review', action='store',
                    help='Review results instead of uploading forecast. '
                         'Include argument e.g. 2022fed')
upload_local = parser.parse_args().local or parser.parse_args().all
upload_test = parser.parse_args().test or parser.parse_args().all
upload_remote = parser.parse_args().remote or parser.parse_args().all
timeseries = parser.parse_args().timeseries
results = parser.parse_args().results
review = parser.parse_args().review
if not (upload_local or upload_test or upload_remote):
    upload_local = True

env = environ.Env()
BASE_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
environ.Env.read_env(os.path.join(BASE_DIR, 'uploads/.env'))
AUTO_EMAIL = env('AUTO_EMAIL')
AUTO_PASSWORD = env('AUTO_PASSWORD')

print(f'Upload initiated: {datetime.datetime.now()}')

if timeseries:
    data = '{"termCode":"' + timeseries + '"}'
    url_part = 'submit-timeseries-update'
elif results:
    data = '{"termCode":"' + results + '"}'
    url_part = 'submit-results-update'
elif review:
    data = '{"termCode":"' + review + '"}'
    url_part = 'submit-review'
else:
    with open("latest_json.dat") as f:
        data = f.read()
        print(f"Report name: {json.loads(data)['reportLabel']}")
        print(f"Report mode: {json.loads(data)['reportMode']}")
        url_part = 'submit-report'

login_data = {'email': AUTO_EMAIL, 'password': AUTO_PASSWORD}

if upload_local:
    print("Sending to local server:")
    response = requests.post('http://localhost:8000/auth-api/v1/auth/login/', data=login_data)
    token = response.cookies['jwt_token']

    headers = {
        'Authorization': 'JWT ' + token
    }

    response = requests.post(f'http://localhost:8000/forecast-api/{url_part}', headers=headers, data=data)
    handle_response(response)

if upload_test:
    print("Sending to remote test server:")
    response = requests.post('https://dendrite.pythonanywhere.com/auth-api/v1/auth/login/', data=login_data)

    token = response.cookies['jwt_token']

    headers = {
        'Authorization': 'JWT ' + token
    }

    response = requests.post(f'https://dendrite.pythonanywhere.com/forecast-api/{url_part}', headers=headers, data=data)
    handle_response(response)

if upload_remote:
    print("Sending to remote server:")
    response = requests.post('https://www.aeforecasts.com/auth-api/v1/auth/login/', data=login_data)

    token = response.cookies['jwt_token']

    headers = {
        'Authorization': 'JWT ' + token
    }

    response = requests.post(f'https://www.aeforecasts.com/forecast-api/{url_part}', headers=headers, data=data)
    handle_response(response)
    
print(f'Upload completed: {datetime.datetime.now()}')