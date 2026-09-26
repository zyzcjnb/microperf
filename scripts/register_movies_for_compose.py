import re
import sys
import urllib.parse
import urllib.request

# Usage: register_movies_for_compose.py <compose-review.lua>
# Registers the movies listed in the workload script via the running media service.
lua_path = sys.argv[1]
with open(lua_path) as f:
    content = f.read()

start = content.find('local movie_titles = {')
if start < 0:
    print('movie_titles table not found in', lua_path)
    sys.exit(1)
brace_count = 0
end = start
for i in range(start, len(content)):
    if content[i] == '{': brace_count += 1
    elif content[i] == '}':
        brace_count -= 1
        if brace_count == 0: end = i; break

table_content = content[start:end+1]
titles = re.findall(r'"([^"]+)"', table_content)

print(f"Registering {len(titles)} movies...")
registered = 0
failed = 0
for i, title in enumerate(titles):
    movie_id = f"bench_{i+1}"
    data = urllib.parse.urlencode({'title': title, 'movie_id': movie_id})
    req = urllib.request.Request(
        'http://127.0.0.1:8080/wrk2-api/movie/register',
        data=data.encode(),
        headers={'Content-Type': 'application/x-www-form-urlencoded'},
        method='POST')
    try:
        with urllib.request.urlopen(req, timeout=10) as resp:
            if resp.status == 200:
                registered += 1
            else:
                failed += 1
    except Exception:
        failed += 1
print(f"Registered: {registered}, failed: {failed}")
