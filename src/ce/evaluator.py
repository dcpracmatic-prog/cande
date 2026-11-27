"""Evaluadores de peso desacoplados: float vs racional exacto."""

from __future__ import annotations

from abc import ABC, abstractmethod
from fractions import Fraction
import math
import struct
from typing import Any, Sequence


class WeightEvaluator(ABC):
    """Contrato mínimo que el solver necesita del evaluador de pesos."""

    @abstractmethod
    def zero(self) -> Any:
        ...

    @abstractmethod
    def from_weight(self, w: float | int | str | Fraction) -> Any:
        ...

    @abstractmethod
    def add(self, a: Any, b: Any) -> Any:
        ...

    @abstractmethod
    def mul_int(self, a: Any, k: int) -> Any:
        ...

    @abstractmethod
    def compare(self, a: Any, b: Any) -> int:
        ...

    @abstractmethod
    def to_float(self, a: Any) -> float:
        ...

    def sum_weights(self, weights: Sequence[Any]) -> Any:
        total = self.zero()
        for w in weights:
            total = self.add(total, w)
        return total

    def greater(self, a: Any, b: Any) -> bool:
        return self.compare(a, b) > 0

    def greater_equal(self, a: Any, b: Any) -> bool:
        return self.compare(a, b) >= 0

    def compare_ratios(self, w1: Any, d1: int, w2: Any, d2: int) -> int:
        """Compara w1/d1 vs w2/d2 mediante productos cruzados (sin división)."""
        left = self.mul_int(w1, d2)
        right = self.mul_int(w2, d1)
        return self.compare(left, right)


class FloatEvaluator(WeightEvaluator):
    """Modo rápido: acumulación en float nativo."""

    def zero(self) -> float:
        return 0.0

    def from_weight(self, w: float | int | str | Fraction) -> float:
        value = float(w)
        if not math.isfinite(value):
            raise ValueError("Peso float no finito no soportado")
        return value

    def add(self, a: float, b: float) -> float:
        return a + b

    def mul_int(self, a: float, k: int) -> float:
        return a * k

    def compare(self, a: float, b: float) -> int:
        if a < b:
            return -1
        if a > b:
            return 1
        return 0

    def to_float(self, a: float) -> float:
        return a


class ExactEvaluator(WeightEvaluator):
    """Modo exacto con fractions.Fraction.

    from_weight(float) conserva el valor binario exacto del IEEE-754
    (incluye subnormales), alineado con el espíritu de RR-PF.
    """

    def zero(self) -> Fraction:
        return Fraction(0)

    def from_weight(self, w: float | int | str | Fraction) -> Fraction:
        if isinstance(w, Fraction):
            return w
        if isinstance(w, int):
            return Fraction(w)
        if isinstance(w, str):
            return Fraction(w)
        if isinstance(w, float):
            if math.isnan(w) or math.isinf(w):
                raise ValueError("Peso float no finito no soportado")
            # Extracción explícita del valor binario IEEE-754
            bits = struct.unpack(">Q", struct.pack(">d", w))[0]
            sign = -1 if (bits >> 63) else 1
            exp = ((bits >> 52) & 0x7FF) - 1023
            mant = bits & ((1 << 52) - 1)
            if exp == -1023:  # subnormal o cero
                if mant == 0:
                    return Fraction(0)
                return Fraction(sign * mant, 1 << (1022 + 52))
            num = (1 << 52) | mant
            shift = exp - 52
            if shift >= 0:
                return Fraction(sign * (num << shift), 1)
            return Fraction(sign * num, 1 << (-shift))
        raise TypeError(f"Tipo de peso no soportado: {type(w)}")

    def add(self, a: Fraction, b: Fraction) -> Fraction:
        return a + b

    def mul_int(self, a: Fraction, k: int) -> Fraction:
        return a * k

    def compare(self, a: Fraction, b: Fraction) -> int:
        if a < b:
            return -1
        if a > b:
            return 1
        return 0

    def to_float(self, a: Fraction) -> float:
        try:
            return float(a)
        except OverflowError:
            limit = float.fromhex("0x1.fffffffffffffp+1023")
            return limit if a.numerator >= 0 else -limit
