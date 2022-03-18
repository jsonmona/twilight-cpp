#include "Rational.h"

#include <algorithm>
#include <cassert>

// TODO: Use binary GCD
template<typename T>
static T gcd(T u, T v) {
    if (u <= 0)
        return v;
    if (v <= 0)
        return u;
    return gcd(v, (u % v));
}

Rational::Rational() : num_(0), den_(1) {}

Rational::Rational(int num, int den) : num_(num), den_(den) {
    assert(num >= 0);
    assert(den > 0);
}

Rational& Rational::operator=(const Rational& copy) {
    num_ = copy.num();
    den_ = copy.den();
    return *this;
}

Rational& Rational::operator=(Rational&& move) noexcept {
    return *this = move;
}

Rational& Rational::operator*=(const Rational& other) {
    long long n = (long long)num_ * other.num_;
    long long d = (long long)den_ * other.den_;
    long long k = gcd(n, d);
    num_ = n / k;
    den_ = d / k;
    assert(num_ == n / k);
    assert(den_ == d / k);
    return *this;
}

Rational Rational::reduce() const {
    int k = gcd(num(), den());
    return Rational(num() / k, den() / k);
}

Rational Rational::inv() const {
    return Rational(den(), num());
}

int Rational::imul(int factor) const {
    return factor * num() / den();
}

long long Rational::imul(long long factor) const {
    return factor * num() / den();
}
