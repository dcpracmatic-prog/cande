#!/usr/bin/env python3
"""Benchmark de calidad MWIS contra oráculo exacto en familias pequeñas.

Ejecuta varias semillas del solver sobre cada instancia fija, no grafos distintos por semilla. Guarda JSON reproducible.
No es un benchmark de estado del arte: mide el gap de C&E contra su óptimo
exacto en instancias donde branch-and-bound puede terminar.

Portado a la API nueva (AdjustedSolver es6, exacto en micro-unidades):
`solve_both` corre el solver una vez y devuelve la misma solución en dos
presentaciones (float en unidades y exacta como Fraction). Los modos
'float' y 'exact' de este script ya no son dos solvers distintos: son esas
dos presentaciones de la misma corrida, por eso sus filas coinciden.
"""
from __future__ import annotations
import argparse, json, statistics, time
from pathlib import Path
import sys
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "src"))
from ce import ExactEvaluator, solve_both, solve_exact
from ce.instances import FAMILIES, make_instance


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--families', nargs='*', default=list(FAMILIES))
    ap.add_argument('--sizes', nargs='*', type=int, default=[9, 12, 14])
    ap.add_argument('--seeds', type=int, default=30, help='número de semillas del solver por instancia fija')
    ap.add_argument('--graph-seeds', nargs='*', type=int, default=[20261126], help='semillas para generar instancias distintas')
    ap.add_argument('--restarts', type=int, default=8)
    ap.add_argument('--perturbations', type=int, default=10)
    ap.add_argument('--signed-weights', action='store_true', help='usar pesos con signo [-2, 8]')
    ap.add_argument('--out', default='bench_families_results.json')
    args = ap.parse_args()
    rows=[]
    for family in args.families:
      for n in args.sizes:
       for graph_seed in args.graph_seeds:
        g = make_instance(family, n, graph_seed)
        if args.signed_weights:
            import random
            wrng = random.Random(graph_seed ^ 0xCE2026)
            signed_weights = [round(-2.0 + 10.0 * wrng.random(), 6) for _ in range(n)]
            g = type(g).from_edges(n, [(u,v) for u in range(n) for v in g.adj[u] if u < v], signed_weights)
        t0=time.perf_counter(); optimum=solve_exact(g); exact_ms=(time.perf_counter()-t0)*1000
        for solver_seed in range(args.seeds):
          for mode in ('float', 'exact'):
            t0=time.perf_counter()
            sol_f, sol_e = solve_both(g, n_restarts=args.restarts, n_perturbations=args.perturbations, seed=solver_seed)
            sol = sol_f if mode == 'float' else sol_e
            elapsed=(time.perf_counter()-t0)*1000
            exact_eval=ExactEvaluator()
            achieved_exact=sum((exact_eval.from_weight(g.weights[v]) for v in sol.vertices), exact_eval.zero())
            weight=float(achieved_exact); opt=float(optimum.weight)
            gap=0.0 if optimum.weight == 0 else max(0.0,float((optimum.weight-achieved_exact)/abs(optimum.weight)))
            rows.append({'family':family,'n':n,'graph_seed':graph_seed,'solver_seed':solver_seed,'mode':mode,
                         'heuristic_weight':weight,'optimal_weight':opt,'gap_ratio':gap,
                         'objective_optimal':achieved_exact==optimum.weight,
                         'tie_break_identical':sol.vertices==optimum.vertices,'signed_weights':args.signed_weights,
                         'time_ms':elapsed,'exact_time_ms':exact_ms,'optimal_vertices':optimum.vertices,
                         'heuristic_vertices':sol.vertices,'independent':g.is_independent(sol.vertices),
                         'exact_nodes':optimum.nodes,'exact_pruned':optimum.pruned})
    out=ROOT/args.out
    out.write_text(json.dumps({'schema':'ce-bench-families-v3','note':'Portado al AdjustedSolver es6: los modos float/exact son dos presentaciones de la misma corrida exacta en micro-unidades.','signed_weights':args.signed_weights,'solver_seeds':args.seeds,'graph_seeds':args.graph_seeds,'rows':rows},indent=2),encoding='utf-8')
    for mode in ('float','exact'):
      subset=[r for r in rows if r['mode']==mode]
      gaps=[r['gap_ratio'] for r in subset]; times=[r['time_ms'] for r in subset]
      print(f"{mode:5s}: cases={len(subset)} mean_gap={statistics.mean(gaps):.6f} "
            f"median_gap={statistics.median(gaps):.6f} optimal_rate={sum(r['objective_optimal'] for r in subset)/len(subset):.3f} "
            f"median_ms={statistics.median(times):.3f}")
    print(f"saved: {out}")

if __name__=='__main__': main()
