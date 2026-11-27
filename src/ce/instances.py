"""Generadores reproducibles de familias MWIS para evaluación."""
from __future__ import annotations
import random
from .graph import Graph


def make_instance(family: str, n: int, seed: int, density: float = 0.2) -> Graph:
    if n < 1:
        raise ValueError("n must be positive")
    rng = random.Random(seed)
    edges: set[tuple[int, int]] = set()
    if family == "erdos_renyi":
        edges = {(i, j) for i in range(n) for j in range(i + 1, n) if rng.random() < density}
    elif family == "path":
        edges = {(i, i + 1) for i in range(n - 1)}
    elif family == "cycle":
        edges = {(i, i + 1) for i in range(n - 1)}
        if n > 2: edges.add((0, n - 1))
    elif family == "complete":
        edges = {(i, j) for i in range(n) for j in range(i + 1, n)}
    elif family == "star":
        edges = {(0, i) for i in range(1, n)}
    elif family == "bipartite":
        cut = n // 2
        edges = {(i, j) for i in range(cut) for j in range(cut, n) if rng.random() < density}
    elif family == "grid":
        width = max(1, int(n ** 0.5))
        edges = {(i, i + 1) for i in range(n) if i % width != width - 1 and i + 1 < n}
        edges |= {(i, i + width) for i in range(max(0, n - width))}
    else:
        raise ValueError(f"unknown family: {family}")
    weights = [round(0.25 + 9.75 * rng.random(), 6) for _ in range(n)]
    return Graph.from_edges(n, sorted(edges), weights)

FAMILIES = ("erdos_renyi", "path", "cycle", "complete", "star", "bipartite", "grid")
