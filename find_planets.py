import urllib.request, json, urllib.parse
for p in ['earth', 'saturn', 'uranus', 'neptune']:
    url = f"https://www.blenderkit.com/api/v1/search/?query={urllib.parse.quote(p)}+asset_type:model"
    data = json.loads(urllib.request.urlopen(url).read())
    print(f"--- {p} ---")
    for r in [x for x in data['results'] if x.get('isFree')][:5]:
        print(f"Name: {r['name']}, ID: {r['assetBaseId']}")
