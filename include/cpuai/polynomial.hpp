#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace cpuai {
struct Example { double x, y; bool operator==(const Example&) const = default; };
inline bool close(double a, double b) {
    return std::isfinite(a) && std::isfinite(b)
        && std::abs(a-b) <= 1e-8 * (1 + std::max(std::abs(a), std::abs(b)));
}
inline void validateExample(Example e) {
    if (!std::isfinite(e.x) || !std::isfinite(e.y)
        || std::abs(e.x) > 1e6 || std::abs(e.y) > 1e12)
        throw std::runtime_error("example outside finite numeric domain");
}
// Exact algebraic identity for this grammar, not equality on teaching samples.
using Polynomial = std::vector<std::int64_t>;
inline void normalize(Polynomial& p) {
    while (p.size() > 1 && p.back() == 0) p.pop_back();
}
inline std::optional<Polynomial> combine(const Polynomial& a, const Polynomial& b, char op) {
    if (a.empty() || b.empty()) throw std::runtime_error("empty polynomial");
    for (const auto* input:{&a,&b}) {
        if (input->size()>17) throw std::runtime_error("polynomial degree limit");
        for (auto c:*input) if (c < -1000000 || c > 1000000)
            throw std::runtime_error("polynomial coefficient limit");
    }
    Polynomial p;
    if (op == '*') {
        if (a.size() + b.size() - 1 > 17) return std::nullopt;
        p.assign(a.size() + b.size() - 1, 0);
        for (std::size_t i=0; i<a.size(); ++i)
            for (std::size_t j=0; j<b.size(); ++j) p[i+j] += a[i]*b[j];
    } else {
        if (op != '+' && op != '-') throw std::runtime_error("unknown operation");
        p.assign(std::max(a.size(), b.size()), 0);
        for (std::size_t i=0; i<a.size(); ++i) p[i] += a[i];
        for (std::size_t i=0; i<b.size(); ++i) p[i] += op == '+' ? b[i] : -b[i];
    }
    normalize(p);
    for (auto c:p) if (std::abs(c) > 1000000) return std::nullopt;
    return p;
}
struct Expr {
    std::string text;
    Polynomial coefficients;
    int depth = 0;
    std::size_t nodes = 1;
};
inline double evaluate(const Expr& e, double x) {
    if (e.coefficients.empty() || !std::isfinite(x) || std::abs(x)>1e6)
        throw std::runtime_error("invalid evaluation input");
    long double y=0;
    for (auto i=e.coefficients.rbegin(); i!=e.coefficients.rend(); ++i) y=y*x+*i;
    const auto out=static_cast<double>(y);
    if (!std::isfinite(out)) throw std::runtime_error("non-finite prediction");
    return out;
}
}
