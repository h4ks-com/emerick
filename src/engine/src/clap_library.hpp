#pragma once

#include <clap/entry.h>
#include <clap/factory/plugin-factory.h>

#include <expected>
#include <filesystem>
#include <memory>
#include <string>

namespace emerick::engine {

// One loaded CLAP bundle with its entry initialized. Instances of the same bundle share it, so we call init and
// deinit once per load. Only the main thread opens and drops libraries.
class ClapLibrary {
public:
    static std::expected<std::shared_ptr<ClapLibrary>, std::string> open(const std::filesystem::path& bundle);

    ClapLibrary(void* handle, const clap_plugin_entry* entry, const clap_plugin_factory* factory);
    ~ClapLibrary();
    ClapLibrary(const ClapLibrary&) = delete;
    ClapLibrary& operator=(const ClapLibrary&) = delete;
    ClapLibrary(ClapLibrary&&) = delete;
    ClapLibrary& operator=(ClapLibrary&&) = delete;

    [[nodiscard]] const clap_plugin_factory& factory() const { return *factory_; }

private:
    void* handle_;
    const clap_plugin_entry* entry_;
    const clap_plugin_factory* factory_;
};

}  // namespace emerick::engine
