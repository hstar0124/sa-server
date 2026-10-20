#include "sa/common/config_source.hpp"

#include <gtest/gtest.h>

#include <cstdlib>
#include <string>

namespace sa::common {
namespace {

// Sets an environment variable for the lifetime of a test. setenv/unsetenv are not thread safe,
// which is acceptable here because gtest runs tests sequentially.
class ScopedEnv {
public:
    ScopedEnv(const char* name, const char* value) : name_(name) {
        ::setenv(name, value, 1);  // NOLINT(concurrency-mt-unsafe)
    }
    ~ScopedEnv() { ::unsetenv(name_); }  // NOLINT(concurrency-mt-unsafe)
    ScopedEnv(const ScopedEnv&) = delete;
    ScopedEnv(ScopedEnv&&) = delete;
    ScopedEnv& operator=(const ScopedEnv&) = delete;
    ScopedEnv& operator=(ScopedEnv&&) = delete;

private:
    const char* name_;
};

constexpr std::string_view kText = R"(
[zone]
world_id = 1
simulation_hz = 20
name = "alpha"
verbose = false
)";

TEST(ConfigSource, ReadsTypedValuesByDottedKey) {
    auto config = ConfigSource::FromText(kText, "");
    ASSERT_TRUE(config.has_value()) << config.error().message;
    EXPECT_EQ(config->Require<std::uint32_t>("zone.simulation_hz").value(), 20U);
    EXPECT_EQ(config->Require<std::string>("zone.name").value(), "alpha");
    EXPECT_EQ(config->Require<bool>("zone.verbose").value(), false);
}

TEST(ConfigSource, MissingAndMistypedKeysAreErrors) {
    auto config = ConfigSource::FromText(kText, "");
    ASSERT_TRUE(config.has_value());
    EXPECT_FALSE(config->Require<int>("zone.absent").has_value());
    EXPECT_FALSE(config->Require<int>("zone.name").has_value());
    EXPECT_EQ(config->GetOr<int>("zone.absent", 42).value(), 42);
    EXPECT_FALSE(config->GetOr<int>("zone.name", 42).has_value()) << "present but not a number";
    EXPECT_FALSE(config->Optional<int>("zone.absent").value().has_value());
}

TEST(ConfigSource, RejectsOutOfRangeIntegers) {
    auto config = ConfigSource::FromText("[a]\nv = 300\n", "");
    ASSERT_TRUE(config.has_value());
    EXPECT_FALSE(config->Require<std::uint8_t>("a.v").has_value());
}

TEST(ConfigSource, EnvironmentOverridesFileValues) {
    const ScopedEnv hz{"SA_TEST__ZONE__SIMULATION_HZ", "30"};
    const ScopedEnv verbose{"SA_TEST__ZONE__VERBOSE", "true"};
    auto config = ConfigSource::FromText(kText, "SA_TEST__");
    ASSERT_TRUE(config.has_value());
    EXPECT_EQ(config->Require<std::uint32_t>("zone.simulation_hz").value(), 30U);
    EXPECT_EQ(config->Require<bool>("zone.verbose").value(), true);
    EXPECT_EQ(config->Require<std::uint64_t>("zone.world_id").value(), 1U);  // untouched
}

TEST(ConfigSource, MalformedOverrideIsAnError) {
    const ScopedEnv hz{"SA_TEST__ZONE__SIMULATION_HZ", "fast"};
    auto config = ConfigSource::FromText(kText, "SA_TEST__");
    ASSERT_TRUE(config.has_value());
    EXPECT_FALSE(config->Require<std::uint32_t>("zone.simulation_hz").has_value());
}

TEST(ConfigSource, SectionsExposeArraysOfTables) {
    auto config = ConfigSource::FromText(R"(
[[zones]]
zone_id = 1
address = "zone-a"
[[zones]]
zone_id = 2
address = "zone-b"
)",
                                         "");
    ASSERT_TRUE(config.has_value());
    auto zones = config->Sections("zones");
    ASSERT_TRUE(zones.has_value()) << zones.error().message;
    ASSERT_EQ(zones->size(), 2U);
    EXPECT_EQ((*zones)[1].Require<std::uint32_t>("zone_id").value(), 2U);
    EXPECT_EQ((*zones)[1].Require<std::string>("address").value(), "zone-b");
    EXPECT_TRUE(config->Sections("absent")->empty());
    EXPECT_FALSE(ConfigSource::FromText("zones = [1, 2]\n", "")->Sections("zones").has_value());
}

TEST(ConfigSource, InvalidTomlIsAnError) {
    EXPECT_FALSE(ConfigSource::FromText("[zone\n", "").has_value());
}

}  // namespace
}  // namespace sa::common
