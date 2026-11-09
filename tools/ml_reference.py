"""Validate bundled bytes and run real desktop LiteRT inference (not ESP32 evidence).

Install ai-edge-litert and numpy in a separate host environment; no ESP-IDF change.
Usage: python tools/ml_reference.py [--output PATH]
"""
from pathlib import Path
import argparse
import hashlib
import json
import math
import re
import numpy as np
from ai_edge_litert.interpreter import Interpreter

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path)
    args=parser.parse_args()
    root=Path(__file__).resolve().parents[1]
    folder=root/'models/hello_world'
    manifest=json.loads((folder/'manifest.json').read_text())
    header=(root/'esp32s3_mtk/components/edge_mtk/sine_models.h').read_text()
    result={'runtime':'desktop ai-edge-litert; not ESP32 target inference','models':[]}
    for item in manifest['models']:
        data=(folder/item['file']).read_bytes()
        assert len(data)==item['bytes'] and hashlib.sha256(data).hexdigest()==item['sha256']
        kind=item['id'].split('-')[1]
        array=re.search(r'ml_sine_'+kind+r'\[\].*?=\s*\{(.*?)\};',header,re.S).group(1)
        assert bytes(int(v,16) for v in re.findall(r'0x([0-9a-f]{2})',array))==data
        interp=Interpreter(model_content=data)
        interp.allocate_tensors()
        i=interp.get_input_details()[0]; o=interp.get_output_details()[0]
        samples=[]
        for x in [0.0,0.5,1.0,1.829175,3.14,4.5,6.0]:
            value=np.array([[x]],dtype=np.float32)
            if i['dtype']==np.int8:
                scale,zp=i['quantization']
                value=np.clip(np.floor(value/scale+0.5)+zp,-128,127).astype(np.int8)
            interp.set_tensor(i['index'],value); interp.invoke()
            y=interp.get_tensor(o['index']).astype(float)
            if o['dtype']==np.int8:
                scale,zp=o['quantization']; y=(y-zp)*scale
            scalar=float(y.reshape(-1)[0]); assert math.isfinite(scalar)
            samples.append({'input':x,'output':scalar})
        assert max(v['output'] for v in samples)-min(v['output'] for v in samples)>0.1
        result['models'].append({'id':item['id'],'sha256':item['sha256'],'samples':samples})
    # Inspect and run the actual existing vector model, verifying embedded bytes too.
    data=(root/'project_v3/models/autoencoder_int8 (2).tflite').read_bytes()
    embedded=(root/'libraries/EdgeCore/src/models/model_data.h').read_text()
    array=embedded.split('{',1)[1].split('}',1)[0]
    assert bytes(int(v,16) for v in re.findall(r'0x([0-9a-fA-F]{2})',array))==data
    interp=Interpreter(model_content=data); interp.allocate_tensors()
    i=interp.get_input_details()[0]; o=interp.get_output_details()[0]
    x=np.array([35/60,0.5,0,32.3/398]*4,dtype=np.float32).reshape(i['shape'])
    if i['dtype']==np.int8:
        scale,zp=i['quantization']; x=np.clip(np.floor(x/scale+0.5)+zp,-128,127).astype(np.int8)
    interp.set_tensor(i['index'],x); interp.invoke()
    y=interp.get_tensor(o['index']).astype(float)
    if o['dtype']==np.int8:
        scale,zp=o['quantization']; y=(y-zp)*scale
    assert y.size==16 and np.isfinite(y).all()
    result['models'].append({'id':'autoencoder','sha256':hashlib.sha256(data).hexdigest(),'bytes':len(data),
        'input_shape':i['shape'].tolist(),'output_shape':o['shape'].tolist(),'outputs':y.reshape(-1).tolist()})
    text=json.dumps(result,indent=2)+'\n'
    if args.output: args.output.write_text(text)
    print(text)

if __name__=='__main__': main()
