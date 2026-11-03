import importlib.util
from pathlib import Path
import contextlib
import io
import unittest
from unittest.mock import patch

path=Path(__file__).resolve().parents[1]/'esp32s3_mtk/tools/load_sensors.py'
spec=importlib.util.spec_from_file_location('sensor_loader',path)
loader=importlib.util.module_from_spec(spec); spec.loader.exec_module(loader)

class Port:
    def __init__(self,replies): self.replies=iter(replies); self.sent=[]
    def write(self,data): self.sent.append(data)
    def flush(self): pass
    def readline(self): return next(self.replies,b'')

class SensorLoaderTests(unittest.TestCase):
    def run_load(self,port,commands):
        with contextlib.redirect_stdout(io.StringIO()): loader.load(port,commands)
    def test_acknowledgements_and_line_endings(self):
        port=Port([b'Commands: RUN / STOP\n',b'STOP\n',b'CLEARED\n',b'CFG: ok (added #0: AHT)\n',b'[SHOW] sampling=0\n',b'RUN\n'])
        self.run_load(port,['STOP','CLEAR','BEGINCFG|name=AHT|ENDCFG','SHOW','RUN'])
        self.assertEqual(port.sent[0],b'STOP\r\n'); self.assertEqual(len(port.sent),5)
    def test_rejected_configuration_does_not_run(self):
        port=Port([b'CFG: add failed (-2)\n'])
        with self.assertRaises(RuntimeError): self.run_load(port,['BEGINCFG|name=AHT|ENDCFG','RUN'])
        self.assertEqual(len(port.sent),1)
    def test_help_text_is_not_an_ack(self):
        port=Port([b'RUN / STOP control sampling\n'])
        with patch.object(loader.time,'monotonic',side_effect=range(20)):
            with self.assertRaises(TimeoutError): self.run_load(port,['STOP'])
    def test_preset_fits_board_console(self):
        cfg=path.parents[2]/'config/three_sensors.cfg'
        lines=[line for line in cfg.read_text().splitlines() if line and not line.startswith('#')]
        self.assertEqual(lines[:2],['STOP','CLEAR']); self.assertEqual(lines[-1],'RUN')
        self.assertEqual(sum(line.startswith('BEGINCFG') for line in lines),3)
        self.assertTrue(all(len(line.encode('ascii'))<512 for line in lines))
    def test_readiness_handshake(self):
        port=Port([b'[BOOT] starting\n',b'Commands:\n'])
        loader.wait_ready(port)
        self.assertEqual(port.sent,[b'HELP\r\n'])
    def test_readiness_timeout_does_not_clear(self):
        port=Port([])
        with patch.object(loader.time,'monotonic',side_effect=range(50)):
            with self.assertRaises(TimeoutError): loader.wait_ready(port)
        self.assertTrue(port.sent and all(line==b'HELP\r\n' for line in port.sent))

if __name__=='__main__': unittest.main()
