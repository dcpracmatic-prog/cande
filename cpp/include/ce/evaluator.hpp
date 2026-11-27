#pragma once
/**
 * c&e — Evaluadores de peso (float y racional exacto).
 *
 * ExactEvaluator conserva exactamente el valor IEEE-754 de cada double y
 * acumula con enteros de precisión arbitraria (Boost.Multiprecision). Así,
 * la suma no depende de un límite int64 ni incurre en desbordamiento silencioso.
 */

#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>

#include <boost/multiprecision/cpp_int.hpp>

namespace ce {

static_assert(std::numeric_limits<double>::is_iec559 &&
              std::numeric_limits<double>::digits == 53 && sizeof(double) == 8,
              "ExactEvaluator requires IEEE-754 binary64 double");

using BigInt = boost::multiprecision::cpp_int;

struct ExactRational {
    BigInt num{0};
    BigInt den{1};

    ExactRational() = default;
    ExactRational(const BigInt& n, const BigInt& d = 1) : num(n), den(d) {
        normalize();
    }
    ExactRational(int64_t n, int64_t d = 1) : num(n), den(d) {
        normalize();
    }

    void normalize() {
        if (den == 0) throw std::invalid_argument("denominador racional cero");
        if (den < 0) {
            num = -num;
            den = -den;
        }
        if (num == 0) {
            den = 1;
            return;
        }
        BigInt g = gcd(abs(num), den);
        num /= g;
        den /= g;
    }

    static BigInt abs(BigInt v) { return v < 0 ? -v : v; }

    static BigInt gcd(BigInt a, BigInt b) {
        a = abs(a);
        b = abs(b);
        while (b != 0) {
            BigInt t = b;
            b = a % b;
            a = t;
        }
        return a;
    }

    double to_double() const;

    std::string numerator_string() const { return num.convert_to<std::string>(); }
    std::string denominator_string() const { return den.convert_to<std::string>(); }
};

// Compatibilidad de nombre para consumidores antiguos. Ya no es de 64 bits.
using Rational64 = ExactRational;

struct FloatEvaluator {
    using weight_t = double;

    weight_t zero() const { return 0.0; }

    weight_t from_weight(double w) const {
        if (!std::isfinite(w)) throw std::invalid_argument("peso float no finito");
        return w;
    }
    weight_t from_weight(int64_t w) const { return static_cast<double>(w); }

    weight_t add(weight_t a, weight_t b) const { return a + b; }
    weight_t mul_int(weight_t a, int k) const { return a * static_cast<double>(k); }

    int compare(weight_t a, weight_t b) const {
        if (a < b) return -1;
        if (a > b) return 1;
        return 0;
    }

    int compare_ratios(weight_t w1, int d1, weight_t w2, int d2) const {
        const double lhs = w1 * static_cast<double>(d2);
        const double rhs = w2 * static_cast<double>(d1);
        if (lhs < rhs) return -1;
        if (lhs > rhs) return 1;
        return 0;
    }

    double to_float(weight_t a) const { return a; }
};

struct ExactEvaluator {
    using weight_t = ExactRational;

    weight_t zero() const { return weight_t{0, 1}; }
    weight_t from_weight(int64_t w) const { return weight_t{w, 1}; }

    weight_t from_weight(double w) const {
        if (!std::isfinite(w))
            throw std::invalid_argument("peso float no finito");
        if (w == 0.0) return zero();

        // Conversión exacta de double a m * 2^e. frexp también cubre
        // subnormales; el desplazamiento se hace sobre cpp_int y nunca sobre
        // un int64, evitando shifts fuera de rango y conversiones UB.
        int exp = 0;
        const double mant = std::frexp(w, &exp);
        constexpr int bits = std::numeric_limits<double>::digits; // 53
        const int64_t m = static_cast<int64_t>(std::ldexp(mant, bits));
        const int shift = exp - bits;
        BigInt numerator = m;
        BigInt denominator = 1;
        if (shift >= 0) numerator <<= shift;
        else denominator <<= -shift;
        return weight_t{numerator, denominator};
    }

    weight_t from_weight(const ExactRational& r) const { return r; }

    weight_t add(const weight_t& a, const weight_t& b) const {
        // Denominador común reducido antes de multiplicar para limitar el
        // crecimiento intermedio de enteros grandes.
        const BigInt g = ExactRational::gcd(a.den, b.den);
        const BigInt a_scale = b.den / g;
        const BigInt b_scale = a.den / g;
        const BigInt numerator = a.num * a_scale + b.num * b_scale;
        const BigInt denominator = a.den * a_scale;
        return weight_t{numerator, denominator};
    }

    weight_t mul_int(const weight_t& a, int k) const {
        if (k == 0 || a.num == 0) return zero();
        return weight_t{a.num * k, a.den};
    }

    int compare(const weight_t& a, const weight_t& b) const {
        const BigInt lhs = a.num * b.den;
        const BigInt rhs = b.num * a.den;
        if (lhs < rhs) return -1;
        if (lhs > rhs) return 1;
        return 0;
    }

    int compare_ratios(const weight_t& w1, int d1,
                       const weight_t& w2, int d2) const {
        if (d1 <= 0 || d2 <= 0)
            throw std::invalid_argument("denominadores de ratio deben ser positivos");
        const BigInt lhs = w1.num * w2.den * d2;
        const BigInt rhs = w2.num * w1.den * d1;
        if (lhs < rhs) return -1;
        if (lhs > rhs) return 1;
        return 0;
    }

    double to_float(const weight_t& a) const { return a.to_double(); }
};

}  // namespace ce
