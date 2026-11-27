#!/usr/bin/env python3
"""Benchmark reproducible del código actual (cpp_int), separado de V6.

Mide Python/C++ sobre la misma instancia (JSON de referencia y su texto
equivalente para el binario). Informa mediana y p95 de múltiples
repeticiones. No compara calidad contra solvers externos.

Portado a la API nueva (AdjustedSolver es6, exacto en micro-unidades):
Python corre `solve_both` y C++ el binario nuevo (`ce_mwis --mode multi`
sobre instancia en texto). Los modos 'float' y 'exact' ya no son dos
solvers distintos: son dos presentaciones de la misma solución exacta.
"""
from __future__ import annotations
import argparse, json, random, statistics, subprocess, sys, time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'src'))
from ce import Graph, solve_both


def timed_python(g, seed, mode, repeats, restarts, perturbations):
    solve_both(g, n_restarts=restarts, n_perturbations=perturbations, seed=seed)
    samples=[]; sol=None
    for _ in range(repeats):
        t=time.perf_counter()
        sol_f, sol_e = solve_both(g, n_restarts=restarts, n_perturbations=perturbations, seed=seed)
        sol = sol_f if mode == 'float' else sol_e
        samples.append((time.perf_counter()-t)*1000)
    return samples,sol


def write_text_instance(path, g):
    edges = [[u, v] for u in range(g.n) for v in g.adj[u] if u < v]
    w_micro = [int(round(w * 1_000_000)) for w in g.weights]
    lines = [f"{g.n} {len(edges)}"]
    lines += [f"{u} {v}" for u, v in edges]
    lines.append(" ".join(str(w) for w in w_micro))
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def parse_cpp_out(out):
    weight = None; time_s = None; vertices = []
    for line in out.splitlines():
        if line.startswith("weight "):
            weight = int(line.split()[1])
        elif line.startswith("time_s "):
            time_s = float(line.split()[1])
        elif line.startswith("vertices"):
            vertices = [int(x) for x in line.split()[1:]]
    if weight is None or time_s is None:
        raise ValueError("salida C++ sin weight/time_s: " + out[:200])
    return {"weight": weight / 1_000_000, "vertices": vertices, "time_s": time_s}


def timed_cpp(binary, txt_path, seed, mode, repeats, restarts, perturbations):
    cmd=[str(binary),'--seed',str(seed),'--reps','3','--mode','multi',
         '--restarts',str(restarts),'--perturbations',str(perturbations),str(txt_path)]
    subprocess.run(cmd,check=True,capture_output=True,text=True,timeout=300)
    internal=[]; wall=[]; payload=None
    for _ in range(repeats):
        t=time.perf_counter(); out=subprocess.run(cmd,check=True,capture_output=True,text=True,timeout=300)
        wall.append((time.perf_counter()-t)*1000)
        payload=parse_cpp_out(out.stdout)
        internal.append(payload['time_s']*1000)
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
        tp=inst_dir/f'er_n{n}_d{args.density:g}_s{seed}.txt'
        write_text_instance(tp, g)
        for mode in ('float','exact'):
          py_samples,py_sol=timed_python(g,seed,mode,args.repeats,args.restarts,args.perturbations)
          cpp_internal,cpp_wall,cpp_payload=timed_cpp(binary,tp,seed,mode,args.repeats,args.restarts,args.perturbations)
          row={'n':n,'m':len(edges),'seed':seed,'density':args.density,'mode':mode,
               'python':stats(py_samples),'cpp_internal':stats(cpp_internal),'cpp_wall':stats(cpp_wall),
               'python_weight':float(py_sol.weight),'cpp_weight':cpp_payload['weight'],
               'python_independent':g.is_independent(py_sol.vertices),
               'cpp_independent':g.is_independent(cpp_payload['vertices']),
               'same_graph_file':str(gp.relative_to(ROOT)),
               'same_graph_txt':str(tp.relative_to(ROOT))}
          row['speedup_python_over_cpp_internal']=row['python']['median_ms']/max(row['cpp_internal']['median_ms'],1e-12)
          rows.append(row)
          print(f"n={n:4d} seed={seed:3d} {mode:5s} Py={row['python']['median_ms']:.3f}ms "
                f"C++={row['cpp_internal']['median_ms']:.3f}ms wall={row['cpp_wall']['median_ms']:.3f}ms "
                f"speedup={row['speedup_python_over_cpp_internal']:.2f}x feasible={row['python_independent'] and row['cpp_independent']}",flush=True)
    payload={'schema':'ce-cpp-int-benchmark-v2','note':'Portado al AdjustedSolver es6 (exacto en micro-unidades, float/exact son presentaciones de la misma solucion); C++ con --mode multi, 3 reps.',
             'repeats':args.repeats,'restarts':args.restarts,'perturbations':args.perturbations,'rows':rows}
    out=ROOT/args.out; out.write_text(json.dumps(payload,indent=2),encoding='utf-8')
    print(f'saved: {out}')
if __name__=='__main__': main()
