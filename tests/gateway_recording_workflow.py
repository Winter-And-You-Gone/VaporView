"""Exercise the shipped client against a running GUI and a simulated SkyCore."""

import csv
import json
import math
from pathlib import Path
import subprocess
import sys
import time


def verify(origin, certificate, session_root):
    client = Path(__file__).resolve().parents[1] / 'tools' / 'gateway_client.py'

    def call(*args):
        proc = subprocess.run([sys.executable, client, origin, '--ca', certificate, *args],
                              capture_output=True, text=True, timeout=45)
        if proc.returncode:
            raise RuntimeError(f'{args}: {proc.stderr} {proc.stdout}')
        return [json.loads(line) for line in proc.stdout.splitlines()]

    def command(action):
        accepted, final = call('command', action)
        assert accepted['status'] == 'accepted' and final['status'] == 'acknowledged'
        assert accepted['id'] == final['id'] and final['sequence'] > 0
        assert call('result', final['id']) == [final]
        print(action, final['id'], 'SkyCore ACK', final['sequence'], flush=True)

    def recording_state(wanted):
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            status, = call('status')
            if status.get('recordingState') == wanted and status['linkConnected']:
                return status
            time.sleep(0.15)
        raise RuntimeError(f'recordingState did not reach {wanted}: {status}')

    recording_state(0)
    command('start-recording')
    session_name = recording_state(1)['sessionName']
    latest, = call('latest')
    assert latest['measurement']['kind'] == 'measurement'
    events = call('stream', '--count', '20')
    assert events[0]['kind'] == 'status' and events[0]['recordingState'] == 1
    observed = [e for e in events if e['kind'] == 'measurement' and e.get('source') == 'fpga-cycle-feature' and e['valid']]
    assert observed and len({e['streamId'] for e in events}) == 1
    sequences = [int(e['sequence']) for e in events]
    assert sequences == sorted(set(sequences))
    command('stop-recording')
    recording_state(0)
    folder = Path(session_root) / session_name
    manifest = json.loads((folder / 'session.json').read_text(encoding='utf-8-sig'))
    with (folder / 'sensors' / 'waveform_features.csv').open(encoding='utf-8-sig', newline='') as stream:
        features = {(r['host_time_us'], int(r['channel_id']), int(r['cycle_id'])): r
                    for r in csv.DictReader(stream) if r['cycle_id']}
    for event in observed:
        row = features[(event['hostTimeUs'], event['channel'], event['cycleId'])]
        assert int(row['quality_flags']) == event['qualityFlags']
        assert math.isclose(float(row['peak']), event['value'], rel_tol=1e-5, abs_tol=1e-7)
    print(session_name, 'WSS measurements matched to Session', len(observed),
          'waveform frames', manifest['counts']['waveform_frames'], flush=True)


if __name__ == '__main__':
    verify(*sys.argv[1:])
