import requests

with open("latest.dat") as f:
    data = f.read()
    print(data)

r = requests.post('http://localhost:8000/forecast-api/api-post', data=data)
print(r.status_code)
print(r.content)