#!/usr/bin/env python3
"""Demostración rápida del MVP c&e."""

from ce import Graph, solve_both


def main():
    print("=== c&e (Causa & Efecto) — Demo MWIS MVP ===\n")

    # Grafo pequeño con pesos heterogéneos
    edges = [(0, 1), (1, 2), (2, 3), (3, 4), (0, 4), (1, 3)]
    weights = [2.5, 1.1, 1.1, 2.5, 0.8]
    g = Graph.from_edges(5, edges=edges, weights=weights)
    print(f"Grafo: {g}")
    print(f"Pesos: {weights}\n")

    sol_f, sol_e = solve_both(g)

    print("Modo float :", sol_f)
    print("Modo exacto:", sol_e)
    print()
    print("Conjunto float  independiente?", g.is_independent(sol_f.vertices))
    print("Conjunto exacto independiente?", g.is_independent(sol_e.vertices))
    print()
    print("Diferencia de peso (float - exacto convertido):",
          float(sol_f.weight) - float(sol_e.weight))


if __name__ == "__main__":
    main()
