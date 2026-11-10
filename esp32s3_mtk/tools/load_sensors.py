"""Load the reviewed three-sensor preset through the normal serial REPL.

Invoking this command explicitly replaces the board's volatile sensors/rules
via STOP/CLEAR. It neither flashes firmware nor changes network credentials.
"""
from pathlib import Path
import argparse
import time
import serial

def wait_ready(connection):
    deadline=time.monotonic()+15
    retry=0
    while time.monotonic()<deadline:
        now=time.monotonic()
        if now>=retry:
            connection.write(b'HELP\r\n'); connection.flush(); retry=now+1
        line=connection.readline().decode('utf-8',errors='replace').strip()
        if line.startswith('Commands:'):
            return
    raise TimeoutError('Board did not answer HELP; check its boot output, firmware and COM port.')

def load(connection, commands):
    for command in commands:
        connection.write((command+'\r\n').encode('ascii'))
        connection.flush()
        expected = 'CFG: ok' if command.startswith('BEGINCFG') else {
            'STOP':'STOP','CLEAR':'CLEARED','RUN':'RUN','SHOW':'[SHOW]'
        }[command]
        deadline=time.monotonic()+5
        while time.monotonic()<deadline:
            line=connection.readline().decode('utf-8',errors='replace').strip()
            if not line:
                continue
            print(line,flush=True)
            if 'CFG: error' in line or 'CFG: add failed' in line or 'Unknown:' in line or 'start-up stopped' in line:
                raise RuntimeError(f'Board refused {command}: {line}')
            if (line.startswith(expected) if command.startswith('BEGINCFG') or command=='SHOW' else line==expected):
                break
        else:
            raise TimeoutError(f'No acknowledgement for {command}')

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port',required=True)
    args=parser.parse_args()
    path=Path(__file__).resolve().parents[2]/'config/three_sensors.cfg'
    commands=[x.strip() for x in path.read_text().splitlines() if x.strip() and not x.lstrip().startswith('#')]
    assert commands[:2]==['STOP','CLEAR'] and commands[-1]=='RUN'
    assert all(len(c)<512 and (c in ['STOP','CLEAR','SHOW','RUN'] or c.startswith('BEGINCFG|')) for c in commands)
    print('Loading three sensors; STOP/CLEAR replaces volatile sensors and rules.',flush=True)
    connection=serial.Serial(port=None,baudrate=115200,timeout=0.2,write_timeout=2)
    connection.dtr=False; connection.rts=False; connection.port=args.port
    with connection:
        connection.reset_input_buffer()
        wait_ready(connection)
        load(connection,commands)
    print('Preset loaded and RUN acknowledged. Open MTK: monitor for readings.')

if __name__=='__main__':
    main()
