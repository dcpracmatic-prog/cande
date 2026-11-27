#include "ce/evaluator.hpp"

#include <cstdint>
#include <cstring>
#include <limits>

namespace ce {
namespace {

BigInt round_quotient_to_even(const BigInt& numerator, const BigInt& denominator) {
    BigInt quotient = numerator / denominator;
    const BigInt remainder = numerator % denominator;
    const BigInt twice_remainder = remainder << 1;
    if (twice_remainder > denominator ||
        (twice_remainder == denominator && (quotient & 1) != 0)) {
        ++quotient;
    }
    return quotient;
}

double from_binary64_bits(uint64_t bits) {
    double value;
    static_assert(sizeof(value) == sizeof(bits), "double must be 64-bit IEEE-754");
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

}  // namespace

double ExactRational::to_double() const {
    if (num == 0) return 0.0;

    const bool negative = num < 0;
    const BigInt numerator = abs(num);
    const BigInt& denominator = den;  // normalize() guarantees denominator > 0
    const uint64_t sign_bit = negative ? (uint64_t(1) << 63) : 0;
    const double max_value = std::numeric_limits<double>::max();

    // e = floor(log2(numerator / denominator)). msb() gives the candidate;
    // one exact comparison corrects it when the ratio is below that power.
    long long exponent = static_cast<long long>(boost::multiprecision::msb(numerator)) -
                         static_cast<long long>(boost::multiprecision::msb(denominator));
    if (exponent >= 0) {
        if (numerator < (denominator << static_cast<unsigned long long>(exponent))) --exponent;
    } else {
        if ((numerator << static_cast<unsigned long long>(-exponent)) < denominator) --exponent;
    }

    // Finite-input sums may exceed DBL_MAX. Saturate the presentation value;
    // the exact numerator/denominator remain available to callers.
    if (exponent > 1023) return negative ? -max_value : max_value;

    BigInt scaled_num = numerator;
    BigInt scaled_den = denominator;
    uint64_t bits = sign_bit;

    if (exponent >= -1022) {
        // Normal: round value * 2^(52 - exponent) to a 53-bit significand.
        const long long shift = 52 - exponent;
        if (shift >= 0) scaled_num <<= static_cast<unsigned long long>(shift);
        else scaled_den <<= static_cast<unsigned long long>(-shift);
        BigInt significand = round_quotient_to_even(scaled_num, scaled_den);

        if (significand >= (BigInt(1) << 53)) {
            // Rounding carried into the next exponent.
            return negative ? -max_value : max_value;
        }
        const uint64_t sig = significand.convert_to<uint64_t>();
        const uint64_t exponent_bits = static_cast<uint64_t>(exponent + 1023);
        const uint64_t fraction_bits = sig - (uint64_t(1) << 52);
        bits |= (exponent_bits << 52) | fraction_bits;
        return from_binary64_bits(bits);
    }

    // Subnormal: every value is an integer multiple of 2^-1074.
    scaled_num <<= 1074;
    const BigInt significand = round_quotient_to_even(scaled_num, scaled_den);
    if (significand == 0) return from_binary64_bits(sign_bit);

    const BigInt normal_boundary = BigInt(1) << 52;
    if (significand >= normal_boundary) {
        // The rounded subnormal reached the smallest normal value.
        bits |= (uint64_t(1) << 52);
    } else {
        bits |= significand.convert_to<uint64_t>();
    }
    return from_binary64_bits(bits);
}

}  // namespace ce
