import importlib.util
from pathlib import Path
import sys
import types
import unittest
from unittest.mock import patch

TOOLS=Path(__file__).resolve().parents[1]/'esp32s3_mtk/tools'
sys.path.insert(0,str(TOOLS))
sys.modules.setdefault('serial',types.SimpleNamespace())
import load_ml

class Connection:
    def __init__(self,lines): self.lines=list(lines); self.writes=[]
    def write(self,b): self.writes.append(b)
    def flush(self): pass
    def readline(self): return self.lines.pop(0) if self.lines else b''

class Tests(unittest.TestCase):
    def test_exact_ack(self):
        c=Connection([b'ML: OK RUN WRONG\n',b'ML: OK RUN\n'])
        load_ml.ml_command(c,'ML RUN')
        self.assertEqual(c.writes,[b'ML RUN\r\n'])
        self.assertFalse(c.lines)
    def test_error(self):
        with self.assertRaises(RuntimeError): load_ml.ml_command(Connection([b'ML: ERROR bad\n']),'ML RUN')
    def test_timeout(self):
        with patch.object(load_ml.time,'monotonic',side_effect=[0,11]):
            with self.assertRaises(TimeoutError): load_ml.ml_command(Connection([]),'ML RUN')
    def test_order(self):
        calls=[]
        with patch.object(load_ml,'load',side_effect=lambda c,cmds:calls.extend(cmds)), patch.object(load_ml,'ml_command',side_effect=lambda c,cmd:calls.append(cmd)):
            load_ml.configure(None,['STOP','CLEAR','SHOW','RUN'],'sine-int8')
        self.assertEqual(calls,['STOP','CLEAR','SHOW','ML PRESET sine-int8','ML RUN','RUN'])

class AhtLightTests(unittest.TestCase):
    def test_only_working_sources(self):
        calls=[]
        with patch.object(load_ml,'load',side_effect=lambda c,cmds:calls.extend(cmds)), patch.object(load_ml,'ml_command',side_effect=lambda c,cmd:calls.append(cmd)):
            load_ml.configure(None,['STOP','CLEAR','BEGINCFG|name=AHT|ENDCFG','BEGINCFG|name=DISTANCE|ENDCFG','SHOW','RUN'],'sine-float','aht-light')
        self.assertFalse(any('name=DISTANCE|' in x for x in calls))
        self.assertIn('ML MODEL sine-float',calls)
        self.assertIn('ML BIND 2 LIGHT light_lux 0 1000',calls)
        self.assertEqual(calls[-2:],['ML RUN','RUN'])
    def test_vector_rejected_before_writes(self):
        with patch.object(load_ml,'load') as send:
            with self.assertRaises(ValueError): load_ml.configure(None,['STOP','CLEAR','RUN'],'autoencoder','aht-light')
            send.assert_not_called()

if __name__=='__main__': unittest.main()
