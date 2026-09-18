"""Compile BLIF Boolean equations for fast full-design Monte Carlo validation."""
from __future__ import annotations
import hashlib
import fcntl
import json
from pathlib import Path
from .hals_comparison import run,write_json


def compile_blif(path):
    with Path(str(path)+'.native.lock').open('w') as lock:
        fcntl.flock(lock,fcntl.LOCK_EX)
        return _compile_blif(path)


def _compile_blif(path):
    path=Path(path);library=Path(str(path)+'.so');metadata=Path(str(path)+'.native.json')
    digest=hashlib.sha256(path.read_bytes()).hexdigest()
    if library.exists() and metadata.exists() and json.loads(metadata.read_text()).get('sha256')==digest:return library
    lines=[];buffer=''
    for line in path.read_text().splitlines():
        if line.endswith('\\'):buffer+=line[:-1]+' ';continue
        lines.append(buffer+line);buffer=''
    ids={}
    def ident(n):
        if n not in ids:ids[n]=len(ids)
        return ids[n]
    inputs=[];outputs=[];raw=[];i=0
    while i<len(lines):
        words=lines[i].split();i+=1
        if not words:continue
        op=words[0]
        if op=='.inputs':inputs=[ident(n) for n in words[1:]]
        elif op=='.outputs':outputs=[ident(n) for n in words[1:]]
        elif op=='.names':
            gate_inputs=[ident(n) for n in words[1:-1]];output=ident(words[-1]);cubes=[];on=True
            while i<len(lines) and not lines[i].startswith('.'):
                truth=lines[i].split();i+=1
                if not truth or truth[0].startswith('#'):continue
                cube,bit=(truth[0],truth[1]) if gate_inputs else ('',truth[0])
                if len(cube)!=len(gate_inputs):raise ValueError('invalid BLIF cube')
                if cubes and on!=(bit=='1'):raise ValueError('mixed BLIF cover polarity')
                on=bit=='1';cubes.append(cube)
            raw.append((gate_inputs,output,cubes,on))
        elif op in ('.latch','.gate','.subckt'):raise ValueError('expected combinational .names BLIF')
    ready=set(inputs);ordered=[]
    while raw:
        waiting=[]
        for gate in raw:
            if set(gate[0])<=ready:ordered.append(gate);ready.add(gate[1])
            else:waiting.append(gate)
        if len(waiting)==len(raw):raise ValueError('cyclic BLIF')
        raw=waiting
    code=['#include <stdint.h>','void hals_eval(const uint32_t *in,uint32_t *out){']
    for i,v in enumerate(inputs):code.append(f'const uint32_t v{v}=(in[{i//32}]>>{i%32})&1u;')
    for gi,out,cubes,on in ordered:
        terms=[]
        for cube in cubes:
            terms.append('('+' & '.join(f'v{v}' if c=='1' else f'(v{v}^1u)' for v,c in zip(gi,cube) if c!='-')+')' if any(c!='-' for c in cube) else '1u')
        expr=' | '.join(terms) if terms else '0u'
        if cubes and not on:expr=f'(({expr})^1u)'
        code.append(f'const uint32_t v{out}={expr};')
    for start in range(0,len(outputs),32):
        expression=' | '.join(f'(v{v}<<{i})' for i,v in enumerate(outputs[start:start+32]))
        code.append(f'out[{start//32}]={expression};')
    code.append('}')
    source=Path(str(path)+'.native.c');source.write_text('\n'.join(code)+'\n')
    timing=run(['gcc','-O1','-shared','-fPIC',str(source),'-o',str(library)],path.parent,path.name+'.native_compile')
    write_json(metadata,dict(sha256=digest,input_bits=len(inputs),output_bits=len(outputs),gates=len(ordered),elapsed_sec=timing['elapsed_sec']))
    return library
