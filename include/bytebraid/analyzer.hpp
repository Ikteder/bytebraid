#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace bytebraid {

enum class IndexBackend {
    memory,
    disk,
};

struct ScanOptions {
    std::uintmax_t min_size = 1;
    std::size_t chunk_min = 2 * 1024;
    std::size_t chunk_average = 8 * 1024;
    std::size_t chunk_max = 32 * 1024;
    double similarity_threshold = 0.65;
    IndexBackend index_backend = IndexBackend::memory;
    std::size_t index_partitions = 64;
    std::filesystem::path temp_directory;
};

struct PhysicalIdentity {
    bool available = false;
    std::uint64_t device = 0;
    std::uint64_t file = 0;
    std::uint64_t reported_links = 1;
};

struct FileInfo {
    std::filesystem::path path;
    std::uintmax_t size = 0;
    std::uint64_t whole_hash = 0;
    std::vector<std::uint64_t> chunks;
    PhysicalIdentity identity;
};

struct ExactGroup {
    std::uintmax_t file_size = 0;
    std::size_t physical_copies = 0;
    bool physical_identity_complete = true;
    std::uint64_t reclaimable_bytes = 0;
    std::vector<std::filesystem::path> paths;
};

struct HardLinkGroup {
    std::uintmax_t file_size = 0;
    std::uint64_t reported_links = 1;
    std::vector<std::filesystem::path> paths;
};

struct NearPair {
    std::filesystem::path left;
    std::filesystem::path right;
    double similarity = 0.0;
    std::size_t shared_chunks = 0;
    std::size_t union_chunks = 0;
};

struct ScanResult {
    std::filesystem::path root;
    std::uint64_t files_seen = 0;
    std::uint64_t bytes_seen = 0;
    std::uint64_t eligible_files = 0;
    std::uint64_t physical_eligible_files = 0;
    std::uint64_t physical_eligible_bytes = 0;
    std::uint64_t identity_unavailable_files = 0;
    std::uint64_t reclaimable_exact_bytes = 0;
    std::uint64_t index_posting_records = 0;
    std::uint64_t index_peak_resident_records = 0;
    std::uint64_t index_partitions_used = 0;
    std::vector<FileInfo> files;
    std::vector<HardLinkGroup> hard_link_groups;
    std::vector<ExactGroup> exact_groups;
    std::vector<NearPair> near_pairs;
    std::vector<std::string> warnings;
};

ScanResult analyze(const std::filesystem::path& root, const ScanOptions& options = {});
std::string to_json(const ScanResult& result, const ScanOptions& options);
std::string format_bytes(std::uint64_t bytes);
std::string index_backend_name(IndexBackend backend);

}  // namespace bytebraid
