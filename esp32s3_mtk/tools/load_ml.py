"""Load current sensors and enable local ML without resetting/flashing the board."""
import argparse
from pathlib import Path
import time
import serial
from load_sensors import wait_ready, load

def ml_command(connection, command):
    connection.write((command+'\r\n').encode('ascii'))
    connection.flush()
    expected='ML: OK '+command[3:]
    deadline=time.monotonic()+10
    while time.monotonic()<deadline:
        line=connection.readline().decode('utf-8',errors='replace').strip()
        if not line:
            continue
        print(line,flush=True)
        if line.startswith('ML: ERROR') or line.startswith('Unknown:'):
            raise RuntimeError(f'{command}: {line}')
        if line==expected:
            return
    raise TimeoutError(f'No exact acknowledgement for {command}; check ML firmware version.')

def configure(connection, commands, model, inputs="all"):
    # Reuse the proven loader; remove its final RUN so ML configuration is stopped.
    if inputs=='aht-light':
        if model=='autoencoder':
            raise ValueError('aht-light currently supports the scalar sine models only')
        commands=[c for c in commands if 'name=DISTANCE|' not in c]
    load(connection,commands[:-1])
    if inputs=='all':
        ml_command(connection,'ML PRESET '+model)
    else:
        ml_command(connection,'ML MODEL '+model)
        ml_command(connection,'ML RESET')
        ml_command(connection,'ML BIND 0 AHT temperature_c -10 50')
        ml_command(connection,'ML BIND 1 AHT humidity_pct 0 100')
        ml_command(connection,'ML BIND 2 LIGHT light_lux 0 1000')
    ml_command(connection,'ML RUN')
    load(connection,['RUN'])

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port',required=True)
    parser.add_argument('--model',choices=['sine-float','sine-int8','autoencoder'],default='sine-float')
    parser.add_argument('--inputs',choices=['all','aht-light'],default='all')
    args=parser.parse_args()
    path=Path(__file__).resolve().parents[2]/'config/three_sensors.cfg'
    commands=[s.strip() for s in path.read_text().splitlines() if s.strip() and not s.lstrip().startswith('#')]
    if commands[:2]!=['STOP','CLEAR'] or commands[-1]!='RUN':
        raise ValueError('Unexpected sensor preset; refusing to apply.')
    connection=serial.Serial(port=None,baudrate=115200,timeout=0.2,write_timeout=2)
    connection.dtr=False; connection.rts=False; connection.port=args.port
    print('Loading sensors and ML; STOP/CLEAR replaces volatile sensor/rule settings.',flush=True)
    with connection:
        connection.reset_input_buffer()
        wait_ready(connection)
        configure(connection,commands,args.model,args.inputs)
    print('ML and sensor RUN acknowledged. Open MTK: monitor; do not press RESET.')

if __name__=='__main__':
    main()
