import json
import sqlite3
import sys
from pathlib import Path

action, directory = sys.argv[1:3]
root = Path(directory)
if action == 'prepare':
    (root / 'config').mkdir(exist_ok=True)
    connection = sqlite3.connect(root / 'config/throne.db')
    connection.execute('CREATE TABLE IF NOT EXISTS settings(key TEXT PRIMARY KEY,value TEXT NOT NULL)')
    connection.execute("INSERT OR REPLACE INTO settings(key,value) VALUES('disable_tray','true')")
    connection.commit()
    connection.close()
    raise SystemExit(0)
connection = sqlite3.connect(root / 'config/throne.db')
if action == 'seed':
    connection.execute("INSERT INTO groups(id,name,url,skip_auto_update,profiles_json) VALUES(901,'Upgrade subscription','https://example.invalid/subscription',1,'[901,902]')")
    for profile_id, mode in [(901, 'raw'), (902, 'wg')]:
        profile = {'type': 'qwdtt', 'server': '203.0.113.20', 'server_port': 56000,
                   'name': 'Upgrade ' + mode, 'mode': mode, 'password': 'upgrade-test',
                   'hashes': ['hash1', 'hash2', 'hash3', 'hash4'], 'workers': 18, 'raw_port': 56003,
                   'device_id': 'upgrade-fixture-' + mode}
        connection.execute('INSERT INTO profiles(id,type,name,gid,outbound_json) VALUES(?,?,?,?,?)',
                           (profile_id, 'qwdtt', profile['name'], 901, json.dumps(profile)))
    connection.execute("INSERT OR REPLACE INTO settings(key,value) VALUES('test_url','http://example.invalid/generate_204')")
    connection.execute("INSERT OR REPLACE INTO settings(key,value) VALUES('allow_beta_update','true')")
    connection.execute("INSERT INTO route_profiles(id,name,default_outbound_id) VALUES(901,'Upgrade route',901)")
    connection.commit()
    (root / 'config/upgrade-sentinel.txt').write_text('preserve portable configuration')
    (root / 'profiles').mkdir(exist_ok=True)
    (root / 'profiles/upgrade-sentinel.txt').write_text('preserve portable profiles')

snapshot = {
    'profiles': connection.execute('SELECT id,type,name,gid,outbound_json FROM profiles WHERE id IN (901,902) ORDER BY id').fetchall(),
    'group': connection.execute('SELECT id,name,url,skip_auto_update,profiles_json FROM groups WHERE id=901').fetchall(),
    'settings': connection.execute("SELECT key,value FROM settings WHERE key IN ('test_url','allow_beta_update') ORDER BY key").fetchall(),
    'routes': connection.execute('SELECT * FROM route_profiles WHERE id=901').fetchall(),
}
snapshot = json.loads(json.dumps(snapshot))
connection.close()
expected = root.parent / 'upgrade-fixture.json'
if action == 'seed':
    expected.write_text(json.dumps(snapshot))
elif action == 'verify':
    assert snapshot == json.loads(expected.read_text()), 'Profiles, subscriptions, settings or routing changed during upgrade'
    assert (root / 'config/upgrade-sentinel.txt').read_text() == 'preserve portable configuration'
    assert (root / 'profiles/upgrade-sentinel.txt').read_text() == 'preserve portable profiles'
    print('RAW/WG profiles, subscription, routing and settings survived the upgrade')
else:
    raise SystemExit('Unknown fixture action')
