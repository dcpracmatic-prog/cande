# c&e — Causa & Efecto

[![tests](https://github.com/dcpracmatic-prog/cande/actions/workflows/tests.yml/badge.svg)](https://github.com/dcpracmatic-prog/cande/actions/workflows/tests.yml)

Solver heurístico para **Maximum Weight Independent Set (MWIS)**, en Python y C++17, con el mismo algoritmo en ambos lenguajes.

Dado un grafo no dirigido con un peso por vértice, encuentra un conjunto independiente (ningún par de vértices unidos por una arista) de peso total alto. Es un problema NP-duro: este solver no garantiza el óptimo, sino una buena solución rápido.

## Qué hace

`AdjustedSolver`: búsqueda multi-arranque con construcción greedy, mejora local, perturbación y recocido simulado.

- **Greedy**: construye desde cero con denominador `1 + 0.5·grado_residual + 0.5·peso_vecinos/media`; temperatura inicial desde la mediana de los pesos (`temp0 = −mediana·0.25/ln(0.5)`).
- **Mejora local**: adición directa, intercambios 1-1, 2-1 y 1-2 (tope 80 candidatos), con conteo incremental de conflictos.
- **Perturbación**: suelta el 25% de la solución y repara con greedy; aceptación por recocido simulado (enfriamiento 0.85).
- **Parada temprana**: corta tras 6 restarts sin mejora. Por defecto 8 restarts × 10 perturbaciones, semilla 42.
- **Pesos exactos**: enteros en micro-unidades (6 decimales). El C++ usa `int64`; el Python usa `Fraction` para la presentación exacta. El peso reportado se verifica desde los vértices, nunca se confía en lo que imprime el solver.
- **Desempate determinista**: mayor peso, después mayor cardinalidad, después orden lexicográfico. Pesos negativos quedan excluidos; la solución vacía (peso 0) es válida.
- **Oráculo exacto y cotas** (`solve_exact`, cota por cobertura de cliques) para validar instancias pequeñas.

## Instalación

Python 3.10 o posterior. Para el backend C++: compilador C++17, `make` y cabeceras Boost.Multiprecision (`cpp_int`, solo para el evaluador exacto de las pruebas).

```bash
python -m venv .venv
source .venv/bin/activate       # Windows: .venv\Scripts\activate
python -m pip install -e ".[test]"
make -C cpp
```

## Uso

### Python

```python
from ce import Graph, solve_both

graph = Graph.from_edges(
    n=5,
    edges=[(0, 1), (1, 2), (2, 3), (3, 4)],
    weights=[4.0, 2.0, 3.0, 5.0, 2.5],
)

sol_float, sol_exact = solve_both(graph)   # misma solución, dos presentaciones
print(sol_float.vertices, sol_float.weight)  # [0, 2, 4] 9.5
print(sol_exact.vertices, sol_exact.weight)  # [0, 2, 4] 19/2
print("independent:", graph.is_independent(sol_float.vertices))
```

`solve_both(graph, n_restarts=6, n_perturbations=8, seed=42)` corre el solver una vez y devuelve la solución como `float` y como `Fraction` exacta. Para control directo, `AdjustedSolver(seed=42, n_restarts=8, n_perturbations=10)` en `ce.solver` recibe un `Graph` con adyacencias como conjuntos y pesos enteros en micro-unidades, y devuelve `(vertices, peso_en_micro_unidades)`.

Ejemplo incluido:

```bash
PYTHONPATH=src python examples/demo_mwis.py
```

### C++

La entrada es texto simple: primera línea `n m`, luego `m` líneas `u v` (0-based), luego los `n` pesos en micro-unidades (enteros).

```bash
./cpp/ce_mwis --seed 42 --reps 3 --mode multi instancia.txt
```

Salida:

```
weight 145000000
time_s 0.003
reps 3
vertices 0 2 3 4 ...
```

Opciones: `--seed N` (42), `--seed-base N` (42), `--reps R` (1; recomendado 3), `--mode multi|cont` (`multi`), `--restarts R` (8), `--perturbations P` (10), `--early-stop E` (6), `--one-to-two 0|1` (1), `--cont-rounds R` (20). El modo `cont` (1 solve + continuaciones) es experimental y no superó al modo `multi` en las mediciones.

## Rendimiento

Medido, no estimado. Todas las cifras son reproducibles con los scripts del repo.

### Contra KaMIS v3.2 (referencia de la literatura)

Lote propio de 21 instancias (Erdős–Rényi n=50/100/200/400 y Barabási–Albert n=200/400, semillas 101/202/303, pesos enteros 1..10), cande C++ (`--seed 42 --reps 3 --mode multi`) contra `weighted_branch_reduce` (exacto, límite 30 s), `mmwis` y `weighted_local_search` (límite 10 s). Corrido en Kaggle, kernel `cande-vs-kamis-comparacion-mwis`, sobre `main` a3e4fa7. El peso de cada solución se recalcula desde los vértices y se verifica independencia.

| Métrica | Valor |
|---|---|
| Gap medio vs óptimo probado (15/21) | **0.211%** |
| Gap mediana vs óptimo probado | **0.000%** |
| Instancias en el óptimo exacto | 8 de 15 |
| Gap medio vs mmwis (21/21) | 0.221% |
| vs mmwis: gana / empata / pierde | 0 / 9 / 12 |
| Tiempo medio cande | **0.063 s** |
| Tiempo medio exacto / mmwis | 8.806 s / 3.05 s |

Por configuración, gap medio vs óptimo: er50 0.26%, er100_p05 0.254%, er100_p20 0.265%, ba200 0.099%, ba400 0.175%. En er200/er400 el exacto agota los 30 s (mejor conocido, no óptimo probado); cande supera ese mejor valor en 4 de 6 casos.

### Familias pequeñas contra el oráculo exacto

`scripts/bench_families.py` (7 familias × n=9/12/14 × 30 semillas, 1,260 casos):

| Métrica | Valor |
|---|---|
| Gap medio / mediana | 0.000000 / 0.000000 |
| Tasa de óptimo | 1.000 |
| Mediana de tiempo | ~2.5 ms |

### Python vs C++

`scripts/bench_cpp_int.py` (grafos ER, n=12..200, mismo algoritmo en ambos lenguajes):

| Métrica | Valor |
|---|---|
| Speedup C++ / Python (mediana) | **1.77x** (rango 1.60x–2.90x) |
| Lote de 21 instancias, tiempo total | C++ 0.92 s / Python 2.15 s |

Python y C++ usan RNG distintos (`random` vs `mt19937`), así que los pesos pueden diferir ligeramente entre lenguajes; lo que se mide es la distribución de gaps y la velocidad, no una reproducción bit a bit.

## Límites

- **Es heurístico**: no hay garantía de optimalidad. En el lote de 21, el gap medio es 0.211% y nunca supera a mmwis en calidad (0 victorias, 9 empates, 12 derrotas); a cambio es ~50x más rápido que mmwis (0.063 s vs 3.05 s de media).
- **Configuración más débil**: ER denso (er100_p20, gap 0.265%).
- **Alcance medido**: n ≤ 400, pesos enteros 1..10, grafos ER/BA y 7 familias pequeñas. Sin mediciones en benchmarks públicos grandes o duros (BHOSLIB, DIMACS, PACE).
- **El oráculo exacto (`solve_exact`)** solo es viable en instancias pequeñas; el exacto de KaMIS agota 30 s en ER200/ER400.
- **Precisión de pesos**: 6 decimales (micro-unidades). Los `float` de Python se redondean a ese grid al entrar al solver.
- **Un solo hilo**, sin GPU. Determinista dada la semilla y el lenguaje.
- **El movimiento 1-2 importa**: desactivarlo (`--one-to-two 0`) empeora el gap medio a 0.282% y lo hace ~14% más lento en el mismo lote.

## Pruebas

```bash
make -C cpp
PYTHONPATH=src python -m pytest -q
make -C cpp test
```

Compila el backend C++, corre 18 pruebas Python (solver, cotas exactas y diferencial Python↔C++ sobre las mismas instancias) y las regresiones C++ del evaluador racional exacto (incluido el caso de 400 pesos sin desbordamiento). El único Makefile del repo es `cpp/Makefile`; los notebooks y el CI usan `make -C cpp`. CI en GitHub Actions (`.github/workflows/tests.yml`).

## Estructura

```
src/ce/            paquete Python (solver, graph, evaluator, exact, bounds, instances, utils)
cpp/src/solver.cpp binario C++ autocontenido (AdjustedSolver es6)
cpp/src/evaluator.cpp  evaluador racional exacto (cpp_int, usado por las pruebas)
cpp/include/ce/    graph.hpp, evaluator.hpp
cpp/tests/         regresiones C++
scripts/           bench_families.py, bench_cpp_int.py, diff_python_cpp.py
tests/             pruebas Python
examples/          demo_mwis.py
```

## Licencia

GPL-3.0-or-later. Consulta `LICENSE`.
