# NOTA — cambio de solver (2026-11-27)

El solver viejo (MWISSolver con evaluadores float/exacto, temp fija 0.05)
fue reemplazado por el AdjustedSolver es6:

- `cpp/src/solver.cpp` = solver.cpp nuevo (autocontenido: Graph en
  micro-unidades, AdjustedSolver con temp_scale 0.25, cooling 0.85, 1-2 con
  tope 80, early-stop 6 restarts sin mejora, multi-arranque con --reps
  y modo --mode multi|cont). Es el binario `ce_mwis`.
- `src/ce/solver.py` = su versión en Python, con
  `DEFAULT_EARLY_STOP_RESTARTS = 6`.

Medido en el lote de 21 instancias (ER/BA, n=50..400, seeds 101/202/303):
C++ multi gap_opt medio 0.211%, mediana 0.000%, 0.91 s; Python 1 rep
gap_opt medio 0.354%, mediana 0.146%, 2.15 s. Todo verificado exacto
con RR-PF. El Python con early-stop 6 gana el óptimo en ER200 s101
(377 -> 383) respecto al early-stop 4.

Pruebas portadas a la API nueva (2026-11-27): `tests/test_solver.py`
(usa `solve_both`; el chequeo de RuntimeError usa `AdjustedSolver` directo
sobre su `Graph`, y el NaN se rechaza en la conversión a micro-unidades),
`tests/test_differential.py` (sin cambios: el script portado sigue
imprimiendo las mismas dos líneas marcadoras), `scripts/diff_python_cpp.py`
(instancia en texto para el binario nuevo, una sola corrida Python que da
la presentación float y la exacta), `cpp/tests/test_exact.cpp` (las
regresiones del solver van a través del binario `ce_mwis`). `make test`
pasa completo: 18 pytest + C++ test_exact OK (Makefile y CMake).

Archivos del solver viejo que quedaron fuera del build (código muerto,
se conservan en el árbol pero nada los compila):
- `cpp/src/main.cpp` (CLI viejo: --graph/--json/--n/--dens, usaba MWISSolver)
- `cpp/include/ce/solver.hpp` (plantilla MWISSolver)
- `cpp/include/ce/solution.hpp` (Solution templada)
- `scripts/bench_families.py`, `scripts/bench_cpp_int.py` (usan la API
  MWISSolver/SolverConfig; pendientes de portar, no los usa `make test`)
