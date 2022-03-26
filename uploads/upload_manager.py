import requests
import environ
import os
import argparse

parser = argparse.ArgumentParser(
    description='Determine trend adjustment parameters')
parser.add_argument('--local', action='store_true',
                    help='Upload to local server only (default)')
parser.add_argument('--remote', action='store_true',
                    help='Upload to remote server only')
parser.add_argument('--all', action='store_true',
                    help='Upload to both local and remote server')
parser.add_argument('--timeseries', action='store',
                    help='Update timeseries instead of uploading forecast')
upload_local = parser.parse_args().local or parser.parse_args().all
upload_remote = parser.parse_args().remote or parser.parse_args().all
timeseries = parser.parse_args().timeseries
if not (upload_local or upload_remote):
    upload_local = True

env = environ.Env()
BASE_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
environ.Env.read_env(os.path.join(BASE_DIR, 'uploads/.env'))
AUTO_EMAIL = env('AUTO_EMAIL')
AUTO_PASSWORD = env('AUTO_PASSWORD')

if timeseries:
    data = '{"termCode":"' + timeseries + '"}'
    url_part = 'submit-timeseries-update'
else:
    with open("latest_json.dat") as f:
        data = f.read()
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
    print(response.content.decode())

if upload_remote:
    print("Sending to remote test server:")
    response = requests.post('https://dendrite.pythonanywhere.com/auth-api/v1/auth/login/', data=login_data)

    token = response.cookies['jwt_token']

    headers = {
        'Authorization': 'JWT ' + token
    }

    response = requests.post(f'https://dendrite.pythonanywhere.com/forecast-api/{url_part}', headers=headers, data=data)
    print(response.content.decode())

if upload_remote:
    print("Sending to remote server:")
    response = requests.post('https://www.aeforecasts.com/auth-api/v1/auth/login/', data=login_data)

    token = response.cookies['jwt_token']

    headers = {
        'Authorization': 'JWT ' + token
    }

    response = requests.post(f'https://www.aeforecasts.com/forecast-api/{url_part}', headers=headers, data=data)
    print(response.content.decode())