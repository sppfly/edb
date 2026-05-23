#pragma once

// src/wal/wal_manager.hpp

#include <cstdint>
#include <mutex>
#include <vector>

#include "storage/io/io_ops.hpp"
#include "transaction/transaction_manager.hpp"
#include "utils/error.hpp"
#include "utils/primitives.hpp"

namespace edb {

enum class WalResourceManager : std::uint8_t {
    Transaction = 1,
    Heap = 2,
};

enum class WalRecordType : std::uint8_t {
    Commit = 1,
    Abort = 2,
    HeapInsert = 3,
    Checkpoint = 4,
};

struct WalAppendRecord {
    u64                   prev_lsn{0};
    TxId                  tx_id{u64{0}};
    WalResourceManager    resource_manager{WalResourceManager::Transaction};
    WalRecordType         record_type{WalRecordType::Commit};
    std::vector<std::byte> payload;
};

struct WalRecord {
    u64                   lsn{0};
    u64                   prev_lsn{0};
    TxId                  tx_id{u64{0}};
    WalResourceManager    resource_manager{WalResourceManager::Transaction};
    WalRecordType         record_type{WalRecordType::Commit};
    std::vector<std::byte> payload;
};

class WalManager {
   public:
    explicit WalManager(StorageIOOps& io) noexcept;

    [[nodiscard]] auto open() -> VoidResult;
    [[nodiscard]] auto append(const WalAppendRecord& record) -> Result<u64>;
    [[nodiscard]] auto read_all() const -> Result<std::vector<WalRecord>>;
    [[nodiscard]] auto flush(u64 lsn) -> VoidResult;
    [[nodiscard]] auto flush_through(u64 lsn) -> VoidResult;
    [[nodiscard]] auto appended_lsn() const -> u64;
    [[nodiscard]] auto flushed_lsn() const -> u64;

   private:
    StorageIOOps* io{nullptr};
    u64 next_offset{0};
    u64 last_lsn{0};
    u64 durable_lsn{0};
    mutable std::mutex mutex;
};

}  // namespace edb