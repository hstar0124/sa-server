#pragma once

#include "sa/common/config_source.hpp"

#include <expected>
#include <string>
#include <string_view>

namespace sa::common {

struct LoggingConfig {
    std::string level = "info";  // spdlog level name: trace, debug, info, warn, error

    [[nodiscard]] static std::expected<LoggingConfig, ConfigError> FromConfig(
        const ConfigSource& config);
};

// Installs the process-wide default logger. `service` appears in every line so that logs
// from several processes can be interleaved and still attributed.
void InitLogging(std::string_view service, const LoggingConfig& config);

}  // namespace sa::common
