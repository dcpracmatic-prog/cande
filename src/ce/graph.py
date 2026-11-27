"""Representación simple de grafo no dirigido con pesos en vértices."""

from __future__ import annotations

from typing import Iterable, List, Sequence, Tuple
import json
from pathlib import Path


class Graph:
    """Grafo no dirigido simple con pesos de vértice.

    Almacenamiento: lista de adyacencia + lista de pesos.
    Índices de vértices: 0 .. n-1.
    """

    def __init__(self, n: int, edges: Iterable[Tuple[int, int]], weights: Sequence[float | int | str]):
        if n <= 0:
            raise ValueError("n must be positive")
        self.n = n
        self.adj: List[List[int]] = [[] for _ in range(n)]
        for u, v in edges:
            if not (0 <= u < n and 0 <= v < n):
                raise ValueError(f"edge ({u},{v}) out of range")
            if u == v:
                continue  # ignoramos bucles
            self.adj[u].append(v)
            self.adj[v].append(u)
        # eliminar duplicados y ordenar para determinismo
        for i in range(n):
            self.adj[i] = sorted(set(self.adj[i]))

        if len(weights) != n:
            raise ValueError("weights length must equal n")
        self.weights = list(weights)

    @classmethod
    def from_edges(
        cls,
        n: int,
        edges: Iterable[Tuple[int, int]],
        weights: Sequence[float | int | str] | None = None,
    ) -> "Graph":
        if weights is None:
            weights = [1.0] * n
        return cls(n, edges, weights)

    @classmethod
    def from_json(cls, path: str | Path) -> "Graph":
        data = json.loads(Path(path).read_text(encoding="utf-8"))
        return cls(
            n=data["n"],
            edges=[tuple(e) for e in data["edges"]],
            weights=data["weights"],
        )

    def degree(self, v: int) -> int:
        return len(self.adj[v])

    def neighbors(self, v: int) -> List[int]:
        return self.adj[v]

    def is_independent(self, vertices: Sequence[int]) -> bool:
        s = set(vertices)
        for v in s:
            for u in self.adj[v]:
                if u in s:
                    return False
        return True

    def __repr__(self) -> str:
        return f"Graph(n={self.n}, m={sum(len(a) for a in self.adj)//2})"
