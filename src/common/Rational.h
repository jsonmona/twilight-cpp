#ifndef TWILIGHT_COMMON_RATIONAL_H
#define TWILIGHT_COMMON_RATIONAL_H

class Rational {
public:
    Rational();
    Rational(int num, int den);
    Rational(const Rational& copy) = default;
    Rational(Rational&& move) noexcept = default;

    Rational& operator=(const Rational& copy);
    Rational& operator=(Rational&& move) noexcept;

    Rational& operator*=(const Rational& other);
    Rational& operator/=(const Rational& other) { return *this *= other.inv(); }

    int num() const { return num_; }
    int den() const { return den_; }

    float toFloat() const { return (float)num_ / den_; }
    double toDouble() const { return (double)num_ / den_; }

    Rational reduce() const;

    Rational inv() const;

    int imul(int factor) const;
    long long imul(long long factor) const;

private:
    int num_, den_;
};

#endif
