#pragma once

// src/storage/engine/engine_factory.hpp

#include <memory>
#include <string_view>

#include "storage/engine/engine.hpp"

namespace edb {

class StorageEngineFactory {
public:
    StorageEngineFactory() = default;
    StorageEngineFactory(const StorageEngineFactory&) = delete;
    StorageEngineFactory& operator=(const StorageEngineFactory&) = delete;
    StorageEngineFactory(StorageEngineFactory&&) = delete;
    StorageEngineFactory& operator=(StorageEngineFactory&&) = delete;
    virtual ~StorageEngineFactory() = default;

    [[nodiscard]] virtual auto open_engine(u32 relation_oid, std::string_view relation_name,
                                           PageStore& page_store, const EngineConfig& config)
        -> Result<std::unique_ptr<StorageEngine>> = 0;
};

class HeapStorageEngineFactory final : public StorageEngineFactory {
public:
    [[nodiscard]] auto open_engine(u32 relation_oid, std::string_view relation_name,
                                   PageStore& page_store, const EngineConfig& config)
        -> Result<std::unique_ptr<StorageEngine>> override;
};

}  // namespace edb