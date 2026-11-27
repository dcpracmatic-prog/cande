#!/usr/bin/env python3
"""
Prueba diferencial Python ↔ C++.

Genera una instancia en Python, la serializa y entrega exactamente ese mismo
JSON a C++. Compara factibilidad y reporta diferencias de calidad entre las
trayectorias heurísticas; no presupone que ambos RNG produzcan la misma solución.

Uso:
  cd c_and_e/cpp && make
  cd ..
  PYTHONPATH=src python scripts/diff_python_cpp.py --bin cpp/ce_mwis --cases 10
"""

from __future__ import annotations

import argparse
import json
import random
import subprocess
import sys
import time
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "src"))

from ce import Graph, MWISSolver, FloatEvaluator, ExactEvaluator, SolverConfig


def make_instance(n: int, dens: float, seed: int):
    rng = random.Random(seed)
    edges = [(i, j) for i in range(n) for j in range(i + 1, n) if rng.random() < dens]
    # Pesos redondeados a seis decimales para facilitar informes legibles.
    weights = [round(0.5 + 4.5 * rng.random(), 6) for _ in range(n)]
    return Graph.from_edges(n, edges, weights)


def run_python(g: Graph, seed: int, restarts: int = 6, perturbs: int = 8):
    cfg = SolverConfig(n_restarts=restarts, n_perturbations=perturbs, seed=seed)
    t0 = time.perf_counter()
    sol_f = MWISSolver(FloatEvaluator(), config=cfg).solve(g)
    t_f = (time.perf_counter() - t0) * 1000
    t0 = time.perf_counter()
    sol_e = MWISSolver(ExactEvaluator(), config=cfg).solve(g)
    t_e = (time.perf_counter() - t0) * 1000
    return {
        "float_vertices": sorted(sol_f.vertices),
        "float_weight": float(sol_f.weight),
        "float_independent": g.is_independent(sol_f.vertices),
        "exact_vertices": sorted(sol_e.vertices),
        "exact_weight": float(sol_e.weight),
        "exact_independent": g.is_independent(sol_e.vertices),
        "time_float_ms": t_f,
        "time_exact_ms": t_e,
    }


def run_cpp(bin_path: Path, graph_path: Path, seed: int,
            restarts: int = 6, perturbs: int = 8):
    """Ejecuta C++ sobre el mismo JSON utilizado para resolver en Python."""
    cmd = [
        str(bin_path),
        "--graph", str(graph_path),
        "--seed", str(seed),
        "--restarts", str(restarts),
        "--perturbs", str(perturbs),
        "--json",
    ]
    try:
        out = subprocess.check_output(cmd, text=True, timeout=60)
        return json.loads(out.strip().splitlines()[-1])
    except Exception as e:
        return {"error": str(e)}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--bin", default="cpp/ce_mwis")
    ap.add_argument("--cases", type=int, default=8)
    ap.add_argument("--n", type=int, default=12)
    ap.add_argument("--dens", type=float, default=0.2)
    args = ap.parse_args()

    bin_path = Path(args.bin)
    has_cpp = bin_path.exists()
    if has_cpp:
        # intentar marcar ejecutable
        try:
            bin_path.chmod(0o755)
        except Exception:
            pass
        # verificar si realmente se puede ejecutar
        try:
            subprocess.check_output([str(bin_path), "--n", "4", "--seed", "1", "--json"], timeout=10)
        except Exception as e:
            print(f"[INFO] Binario C++ presente pero no ejecutable en este entorno: {e}")
            print("       Se reportan solo resultados Python.\n")
            has_cpp = False
    else:
        print("[INFO] Binario C++ no encontrado. Solo Python.\n")

    print(f"=== Diferencial  n={args.n}  dens={args.dens}  casos={args.cases} ===\n")
    print(f"{'caso':>4} {'seed':>6} | {'PyF sz':>6} {'PyF w':>9} {'PyE sz':>6} {'PyE w':>9} | ind | {'msF':>7} {'msE':>7}")
    print("-" * 90)

    all_ok = True
    times_f, times_e = [], []
    for c in range(args.cases):
        seed = 9000 + c * 17
        g = make_instance(args.n, args.dens, seed)
        py = run_python(g, seed)
        times_f.append(py["time_float_ms"])
        times_e.append(py["time_exact_ms"])

        ind = py["float_independent"] and py["exact_independent"]
        if not ind:
            all_ok = False

        print(f"{c:4d} {seed:6d} | {len(py['float_vertices']):6d} {py['float_weight']:9.4f} "
              f"{len(py['exact_vertices']):6d} {py['exact_weight']:9.4f} | {str(ind):>3} | "
              f"{py['time_float_ms']:7.1f} {py['time_exact_ms']:7.1f}")

        if has_cpp:
            # Serializar la instancia generada en Python; no generar un segundo
            # grafo con mt19937 (que no reproduce random.Random de Python).
            with tempfile.TemporaryDirectory(prefix="ce_diff_") as tmp:
                graph_path = Path(tmp) / "instance.json"
                graph_path.write_text(json.dumps({
                    "n": g.n,
                    "edges": [[u, v] for u in range(g.n) for v in g.adj[u] if u < v],
                    "weights": [float(w) for w in g.weights],
                }), encoding="utf-8")
                cpp = run_cpp(bin_path, graph_path, seed)
            if "error" in cpp:
                print(f"         C++ error: {cpp['error']}")
                all_ok = False
            else:
                cpp_ok = cpp.get("float_independent") and cpp.get("exact_independent")
                d_float = cpp.get("float_weight", 0.0) - py["float_weight"]
                d_exact = cpp.get("exact_weight", 0.0) - py["exact_weight"]
                print(f"         C++ misma_instancia=True float_size={cpp.get('float_size')} "
                      f"exact_size={cpp.get('exact_size')} dW_float={d_float:+.6g} "
                      f"dW_exact={d_exact:+.6g} | factible={bool(cpp_ok)}")
                # Las semillas se mantienen, pero Python random.Random y C++
                # mt19937 siguen trayectorias distintas. Se compara factibilidad
                # y se informa delta de calidad; no se exige identidad de solución.
                if not cpp_ok:
                    all_ok = False

    print("-" * 90)
    print(f"Python  avg float: {sum(times_f)/len(times_f):.1f} ms   avg exact: {sum(times_e)/len(times_e):.1f} ms")
    print(f"Todas las soluciones reportadas son independientes: {all_ok}")
    print("Nota: el diferencial usa el mismo grafo; no exige igualdad de trayectorias RNG.")
    return 0 if all_ok else 1


if __name__ == "__main__":
    sys.exit(main())
