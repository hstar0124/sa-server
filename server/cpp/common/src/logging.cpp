#include "sa/common/logging.hpp"

#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <memory>

namespace sa::common {

std::expected<LoggingConfig, ConfigError> LoggingConfig::FromConfig(const ConfigSource& config) {
    LoggingConfig out;
    const auto level = config.GetOr<std::string>("logging.level", out.level);
    if (!level) {
        return std::unexpected(level.error());
    }
    out.level = *level;
    if (spdlog::level::from_str(out.level) == spdlog::level::off && out.level != "off") {
        return std::unexpected(
            ConfigError{"logging.level '" + out.level + "' is not a known level"});
    }
    return out;
}

void InitLogging(std::string_view service, const LoggingConfig& config) {
    auto logger = spdlog::stdout_color_mt(std::string{service});
    logger->set_level(spdlog::level::from_str(config.level));
    logger->set_pattern("%Y-%m-%dT%H:%M:%S.%eZ %^%l%$ [%n] %v");
    spdlog::set_default_logger(std::move(logger));
}

}  // namespace sa::common
