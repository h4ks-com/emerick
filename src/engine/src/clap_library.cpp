#include "clap_library.hpp"

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include <clap/version.h>

#include <map>
#include <system_error>

namespace emerick::engine {

namespace {

// A macOS bundle keeps its binary in Contents/MacOS; on Linux and Windows the bundle is the binary.
std::filesystem::path binaryOf(const std::filesystem::path& bundle) {
    const std::filesystem::path macos = bundle / "Contents" / "MacOS";
    std::error_code error;
    if (!std::filesystem::is_directory(macos, error)) {
        return bundle;
    }
    for (const auto& entry : std::filesystem::directory_iterator(macos, error)) {
        if (entry.is_regular_file(error)) {
            return entry.path();
        }
    }
    return bundle;
}

#ifdef _WIN32
void* openLibrary(const std::filesystem::path& path) { return LoadLibraryW(path.c_str()); }

// GetProcAddress returns a function pointer type for every symbol, so we cast it back to the data symbol it names.
const void* findSymbol(void* handle, const char* name) {
    return reinterpret_cast<const void*>(GetProcAddress(static_cast<HMODULE>(handle), name));
}

void closeLibrary(void* handle) { FreeLibrary(static_cast<HMODULE>(handle)); }

std::string loadError() { return "error " + std::to_string(GetLastError()); }
#else
void* openLibrary(const std::filesystem::path& path) { return dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL); }

const void* findSymbol(void* handle, const char* name) { return dlsym(handle, name); }

void closeLibrary(void* handle) { dlclose(handle); }

std::string loadError() {
    const char* error = dlerror();
    return error != nullptr ? error : "unknown error";
}
#endif

std::map<std::filesystem::path, std::weak_ptr<ClapLibrary>>& openLibraries() {
    static std::map<std::filesystem::path, std::weak_ptr<ClapLibrary>> libraries;
    return libraries;
}

}  // namespace

std::expected<std::shared_ptr<ClapLibrary>, std::string> ClapLibrary::open(const std::filesystem::path& bundle) {
    auto& libraries = openLibraries();
    if (auto alive = libraries[bundle].lock()) {
        return alive;
    }
    void* handle = openLibrary(binaryOf(bundle));
    if (handle == nullptr) {
        return std::unexpected("cannot load " + bundle.string() + ": " + loadError());
    }
    const auto* entry = static_cast<const clap_plugin_entry*>(findSymbol(handle, "clap_entry"));
    if (entry == nullptr || !clap_version_is_compatible(entry->clap_version)) {
        closeLibrary(handle);
        return std::unexpected(bundle.string() + " is not a CLAP plugin this host can load");
    }
    if (!entry->init(bundle.string().c_str())) {
        closeLibrary(handle);
        return std::unexpected(bundle.string() + " failed to initialize");
    }
    const auto* factory =
        static_cast<const clap_plugin_factory*>(entry->get_factory(std::data(CLAP_PLUGIN_FACTORY_ID)));
    if (factory == nullptr) {
        entry->deinit();
        closeLibrary(handle);
        return std::unexpected(bundle.string() + " has no plugin factory");
    }
    auto library = std::make_shared<ClapLibrary>(handle, entry, factory);
    libraries[bundle] = library;
    return library;
}

ClapLibrary::ClapLibrary(void* handle, const clap_plugin_entry* entry, const clap_plugin_factory* factory)
    : handle_(handle), entry_(entry), factory_(factory) {}

ClapLibrary::~ClapLibrary() {
    entry_->deinit();
    closeLibrary(handle_);
}

}  // namespace emerick::engine
