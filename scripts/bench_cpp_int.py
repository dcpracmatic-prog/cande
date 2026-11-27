#!/usr/bin/env python3
"""Benchmark reproducible del código actual (cpp_int), separado de V6.

Mide Python/C++ float y exact sobre el mismo JSON. Informa mediana y p95 de
múltiples repeticiones. No compara calidad contra solvers externos.
"""
from __future__ import annotations
import argparse, json, random, statistics, subprocess, sys, time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'src'))
from ce import Graph, MWISSolver, FloatEvaluator, ExactEvaluator, SolverConfig


def timed_python(g, seed, mode, repeats, restarts, perturbations):
    ev=FloatEvaluator() if mode=='float' else ExactEvaluator()
    cfg=SolverConfig(n_restarts=restarts,n_perturbations=perturbations,seed=seed)
    MWISSolver(ev,config=cfg).solve(g)
    samples=[]; sol=None
    for _ in range(repeats):
        t=time.perf_counter(); sol=MWISSolver(ev,config=cfg).solve(g)
        samples.append((time.perf_counter()-t)*1000)
    return samples,sol


def timed_cpp(binary, graph_path, seed, mode, repeats, restarts, perturbations):
    cmd=[str(binary),'--graph',str(graph_path),'--seed',str(seed),'--restarts',str(restarts),
         '--perturbs',str(perturbations),'--only',mode,'--json']
    subprocess.run(cmd,check=True,capture_output=True,text=True,timeout=300)
    internal=[]; wall=[]; payload=None
    for _ in range(repeats):
        t=time.perf_counter(); out=subprocess.run(cmd,check=True,capture_output=True,text=True,timeout=300)
        wall.append((time.perf_counter()-t)*1000)
        payload=json.loads(out.stdout.strip().splitlines()[-1])
        internal.append(payload['elapsed_ms_float' if mode=='float' else 'elapsed_ms_exact'])
    return internal,wall,payload


def stats(xs):
    return {'median_ms':statistics.median(xs),'p95_ms':sorted(xs)[min(len(xs)-1,int(.95*(len(xs)-1)))],
            'min_ms':min(xs),'max_ms':max(xs)}


def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--sizes',nargs='*',type=int,default=[12,50,100,200])
    ap.add_argument('--seeds',nargs='*',type=int,default=[42,123])
    ap.add_argument('--density',type=float,default=.2)
    ap.add_argument('--repeats',type=int,default=5)
    ap.add_argument('--restarts',type=int,default=6)
    ap.add_argument('--perturbations',type=int,default=8)
    ap.add_argument('--bin',default='cpp/ce_mwis')
    ap.add_argument('--out',default='bench_cpp_int_results.json')
    args=ap.parse_args()
    binary=(ROOT/args.bin).resolve()
    if not binary.exists(): raise SystemExit(f'C++ binary missing: {binary}; run make -C cpp first')
    inst_dir=ROOT/'benchmarks'/'instances'; inst_dir.mkdir(parents=True,exist_ok=True)
    rows=[]
    for n in args.sizes:
      for seed in args.seeds:
        rng=random.Random(seed)
        edges=[[i,j] for i in range(n) for j in range(i+1,n) if rng.random()<args.density]
        weights=[round(.5+4.5*rng.random(),6) for _ in range(n)]
        g=Graph.from_edges(n,edges,weights)
        gp=inst_dir/f'er_n{n}_d{args.density:g}_s{seed}.json'
        gp.write_text(json.dumps({'n':n,'edges':edges,'weights':weights},separators=(',',':')))
        for mode in ('float','exact'):
          py_samples,py_sol=timed_python(g,seed,mode,args.repeats,args.restarts,args.perturbations)
          cpp_internal,cpp_wall,cpp_payload=timed_cpp(binary,gp,seed,mode,args.repeats,args.restarts,args.perturbations)
          row={'n':n,'m':len(edges),'seed':seed,'density':args.density,'mode':mode,
               'python':stats(py_samples),'cpp_internal':stats(cpp_internal),'cpp_wall':stats(cpp_wall),
               'python_weight':float(py_sol.weight),'cpp_weight':cpp_payload['float_weight' if mode=='float' else 'exact_weight'],
               'python_independent':g.is_independent(py_sol.vertices),
               'cpp_independent':cpp_payload['float_independent' if mode=='float' else 'exact_independent'],
               'same_graph_file':str(gp.relative_to(ROOT))}
          row['speedup_python_over_cpp_internal']=row['python']['median_ms']/max(row['cpp_internal']['median_ms'],1e-12)
          rows.append(row)
          print(f"n={n:4d} seed={seed:3d} {mode:5s} Py={row['python']['median_ms']:.3f}ms "
                f"C++={row['cpp_internal']['median_ms']:.3f}ms wall={row['cpp_wall']['median_ms']:.3f}ms "
                f"speedup={row['speedup_python_over_cpp_internal']:.2f}x feasible={row['python_independent'] and row['cpp_independent']}",flush=True)
    payload={'schema':'ce-cpp-int-benchmark-v1','note':'Mediciones nuevas sobre cpp_int; no comparables directamente con V6 int64.',
             'repeats':args.repeats,'restarts':args.restarts,'perturbations':args.perturbations,'rows':rows}
    out=ROOT/args.out; out.write_text(json.dumps(payload,indent=2),encoding='utf-8')
    print(f'saved: {out}')
if __name__=='__main__': main()
