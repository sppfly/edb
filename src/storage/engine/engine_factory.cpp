// src/storage/engine/engine_factory.cpp

#include "storage/engine/engine_factory.hpp"

#include <expected>
#include <memory>

#include "storage/engine/heap/heap_engine.hpp"

namespace edb {

auto HeapStorageEngineFactory::open_engine(u32 relation_oid, std::string_view relation_name,
                                           PageStore& page_store, const EngineConfig& config)
    -> Result<std::unique_ptr<StorageEngine>> {
    (void)relation_oid;
    (void)relation_name;

    auto engine = std::make_unique<EdbHeapEngine>();
    if (auto status = engine->open(page_store, config); !status) {
        return std::unexpected(status.error());
    }
    return std::unique_ptr<StorageEngine>{std::move(engine)};
}

}  // namespace edb