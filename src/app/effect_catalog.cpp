#include "emerick/app/effect_catalog.hpp"

#include <algorithm>
#include <cmath>
#include <ranges>
#include <string_view>

#include "emerick/engine/effects.hpp"

namespace emerick::app {

namespace {

constexpr double kDisplayRate = 48000.0;

QString kindName(engine::ParamKind kind) {
    switch (kind) {
        case engine::ParamKind::toggle:
            return QStringLiteral("toggle");
        case engine::ParamKind::choice:
            return QStringLiteral("choice");
        case engine::ParamKind::number:
            break;
    }
    return QStringLiteral("number");
}

QString text(std::string_view value) { return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size())); }

}  // namespace

QVariantList EffectCatalog::params(const QString& type) {
    const engine::EffectSpec* spec = engine::findEffect(type.toStdString());
    if (spec == nullptr) {
        return {};
    }
    return spec->params | std::views::transform([](const engine::EffectParam& described) -> QVariant {
               return QVariantMap{
                   {"id", text(described.id)},
                   {"title", text(described.title)},
                   {"unit", text(described.unit)},
                   {"minimum", described.minimum},
                   {"maximum", described.maximum},
                   {"defaultValue", described.defaultValue},
                   {"kind", kindName(described.kind)},
                   {"logarithmic", described.logarithmic},
                   {"choices", described.choices | std::views::transform(text) | std::ranges::to<QStringList>()}};
           }) |
           std::ranges::to<QVariantList>();
}

bool EffectCatalog::reportsReduction(const QString& type) {
    const engine::EffectSpec* spec = engine::findEffect(type.toStdString());
    return spec != nullptr && spec->reportsReduction;
}

QVariantList EffectCatalog::eqCurve(const QVariantMap& params, int points) {
    const engine::EffectSpec& spec = engine::effectSpec(engine::EffectType::eq);
    engine::EffectParams values = engine::defaultParams(spec);
    for (std::size_t index = 0; index < spec.params.size(); ++index) {
        const QVariant given = params.value(text(spec.params[index].id));
        if (given.isValid()) {
            values.at(index) = given.toFloat();
        }
    }
    const double low = std::log(engine::kMinFrequency);
    const double high = std::log(engine::kMaxFrequency);
    const int count = std::max(points, 2);
    return std::views::iota(0, count) | std::views::transform([&](int point) -> QVariant {
               const double x = static_cast<double>(point) / static_cast<double>(count - 1);
               return QVariantMap{
                   {"x", x}, {"db", engine::eqResponseDb(values, std::exp(std::lerp(low, high, x)), kDisplayRate)}};
           }) |
           std::ranges::to<QVariantList>();
}

}  // namespace emerick::app
