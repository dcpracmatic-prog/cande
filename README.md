# c&e — Causa & Efecto

Solver heurístico para **Maximum Weight Independent Set (MWIS)**, con implementación Python y backend C++17. Ambos ofrecen evaluación de pesos en modo `float` y racional exacto.

## Características

- Búsqueda multi-start con construcción greedy, mejora local e iteraciones de perturbación.
- Movimientos locales de adición, intercambio 1-1, 2-1 y 1-2.
- Evaluadores intercambiables: `FloatEvaluator` y `ExactEvaluator`.
- Desempate determinista: mayor peso, después mayor cardinalidad y finalmente orden lexicográfico.
- Pesos negativos admitidos; una solución independiente puede ser vacía.
- Oráculo exacto Python y cota superior por cobertura de cliques para validar instancias pequeñas.

C&E es un **solver heurístico**: no garantiza encontrar el óptimo global. `solve_exact` está destinado a instancias pequeñas y evaluación, no a resolver grafos grandes.

## Instalación

Requiere Python 3.10 o posterior.

```bash
python -m venv .venv
source .venv/bin/activate       # Windows: .venv\\Scripts\\activate
python -m pip install -e ".[test]"
```

## Uso en Python

```python
from ce import Graph, MWISSolver, FloatEvaluator, ExactEvaluator

# Grafo no dirigido: aristas y pesos por vértice.
graph = Graph.from_edges(
    n=5,
    edges=[(0, 1), (1, 2), (2, 3), (3, 4)],
    weights=[4.0, 2.0, 3.0, 5.0, 2.5],
)

solver = MWISSolver(
    evaluator=FloatEvaluator(),
    n_restarts=8,
    n_perturbations=10,
    seed=42,
)
solution = solver.solve(graph)
print(solution.vertices, solution.weight)
print("independent:", graph.is_independent(solution.vertices))

# La evaluación racional conserva exactamente los pesos binarios float.
exact_solution = MWISSolver(ExactEvaluator(), seed=42).solve(graph)
print(exact_solution.vertices, exact_solution.weight)
```

También puedes ejecutar el ejemplo incluido:

```bash
python examples/demo_mwis.py
```

## Backend C++

Requiere compilador C++17, `make` y cabeceras Boost.Multiprecision.

```bash
make -C cpp
./cpp/ce_mwis --n 12 --dens 0.2 --seed 42 --json
```

Para resolver un grafo compartido desde JSON:

```bash
./cpp/ce_mwis --graph graph.json --seed 42 --json
```

El JSON debe contener `n`, `edges` y `weights`, por ejemplo:

```json
{
  "n": 3,
  "edges": [[0, 1], [1, 2]],
  "weights": [4.0, 1.0, 3.0]
}
```

## Pruebas

Instala el extra de pruebas y ejecuta desde la raíz:

```bash
python -m pip install -e ".[test]"
make test
```

`make test` compila el backend C++, ejecuta las pruebas Python, compara ambas implementaciones sobre las mismas instancias y ejecuta las pruebas C++.

## Evaluación experimental

Calidad frente al oráculo exacto en familias pequeñas:

```bash
PYTHONPATH=src python scripts/bench_families.py \
  --sizes 9 12 14 --seeds 30 --graph-seeds 42 123
```

Tiempo de Python y C++ sobre los mismos grafos:

```bash
make -C cpp
PYTHONPATH=src python scripts/bench_cpp_int.py \
  --sizes 12 50 100 --seeds 42 123 --repeats 5
```

Los resultados se generan localmente y no se incluyen como cifras de rendimiento predeterminadas del proyecto.

## Contrato numérico

- `FloatEvaluator` usa aritmética IEEE-754.
- `ExactEvaluator` representa exactamente los valores binarios `float` mediante `fractions.Fraction` en Python y `boost::multiprecision::cpp_int` en C++.
- La evaluación y comparación de pesos son exactas en ese modo. La diversificación y la aceptación probabilística de la búsqueda utilizan números de punto flotante; por ello, el modo exacto no implica trayectorias de búsqueda totalmente racionales.
- El campo decimal de salida C++ es una representación aproximada para presentación; el JSON también expone numerador y denominador racionales exactos.

## Licencia

GPL-3.0-or-later. Consulta `LICENSE`.
