#include "bytebraid/analyzer.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/stat.h>
#endif

namespace bytebraid {
namespace {

constexpr std::uint64_t kFnvOffset = 14695981039346656037ULL;
constexpr std::uint64_t kFnvPrime = 1099511628211ULL;

std::uint64_t splitmix64(std::uint64_t& state) {
    state += 0x9e3779b97f4a7c15ULL;
    auto value = state;
    value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31U);
}

const std::array<std::uint64_t, 256>& gear_table() {
    static const auto table = [] {
        std::array<std::uint64_t, 256> values{};
        std::uint64_t state = 0x4259544542524149ULL;  // "BYTEBRAI"
        for (auto& value : values) {
            value = splitmix64(state);
        }
        return values;
    }();
    return table;
}

bool is_power_of_two(std::size_t value) {
    return value != 0 && (value & (value - 1)) == 0;
}

void validate(const ScanOptions& options) {
    if (options.chunk_min == 0 || options.chunk_min > options.chunk_average ||
        options.chunk_average > options.chunk_max) {
        throw std::invalid_argument("chunk sizes must satisfy 0 < min <= average <= max");
    }
    if (!is_power_of_two(options.chunk_average)) {
        throw std::invalid_argument("average chunk size must be a power of two");
    }
    if (!(options.similarity_threshold > 0.0 && options.similarity_threshold < 1.0)) {
        throw std::invalid_argument("similarity threshold must be between 0 and 1");
    }
    if (options.index_partitions < 2 || options.index_partitions > 4096 ||
        !is_power_of_two(options.index_partitions)) {
        throw std::invalid_argument("index partitions must be a power of two between 2 and 4096");
    }
}

std::string path_text(const std::filesystem::path& path) {
    return path.generic_string();
}

std::string json_escape(const std::string& input) {
    std::ostringstream out;
    for (const unsigned char c : input) {
        switch (c) {
            case '"': out << "\\\""; break;
            case '\\': out << "\\\\"; break;
            case '\b': out << "\\b"; break;
            case '\f': out << "\\f"; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default:
                if (c < 0x20U) {
                    out << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                        << static_cast<int>(c) << std::dec;
                } else {
                    out << static_cast<char>(c);
                }
        }
    }
    return out.str();
}

std::uint64_t finish_chunk(std::uint64_t hash, std::size_t length) {
    hash ^= static_cast<std::uint64_t>(length);
    hash *= kFnvPrime;
    return hash;
}

PhysicalIdentity read_physical_identity(const std::filesystem::path& path) {
    PhysicalIdentity identity;
#ifdef _WIN32
    const auto handle = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                    nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        return identity;
    }
    BY_HANDLE_FILE_INFORMATION information{};
    if (GetFileInformationByHandle(handle, &information) != 0) {
        identity.available = true;
        identity.device = information.dwVolumeSerialNumber;
        identity.file = (static_cast<std::uint64_t>(information.nFileIndexHigh) << 32U) |
                        information.nFileIndexLow;
        identity.reported_links = information.nNumberOfLinks;
    }
    CloseHandle(handle);
#else
    struct stat information {};
    if (::stat(path.c_str(), &information) == 0) {
        identity.available = true;
        identity.device = static_cast<std::uint64_t>(information.st_dev);
        identity.file = static_cast<std::uint64_t>(information.st_ino);
        identity.reported_links = static_cast<std::uint64_t>(information.st_nlink);
    }
#endif
    return identity;
}

using IdentityKey = std::pair<std::uint64_t, std::uint64_t>;

IdentityKey identity_key(const PhysicalIdentity& identity) {
    return {identity.device, identity.file};
}

FileInfo fingerprint_file(const std::filesystem::path& path, std::uintmax_t size,
                          const ScanOptions& options, const PhysicalIdentity& identity) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("cannot open file");
    }

    FileInfo info;
    info.path = path;
    info.size = size;
    info.whole_hash = kFnvOffset;
    info.identity = identity;

    std::array<char, 64 * 1024> buffer{};
    std::uint64_t rolling = 0;
    std::uint64_t chunk_hash = kFnvOffset;
    std::size_t chunk_length = 0;
    const auto mask = static_cast<std::uint64_t>(options.chunk_average - 1);
    const auto& gear = gear_table();

    while (input) {
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto count = input.gcount();
        for (std::streamsize index = 0; index < count; ++index) {
            const auto byte = static_cast<unsigned char>(buffer[static_cast<std::size_t>(index)]);
            info.whole_hash ^= byte;
            info.whole_hash *= kFnvPrime;
            chunk_hash ^= byte;
            chunk_hash *= kFnvPrime;
            rolling = (rolling << 1U) + gear[byte];
            ++chunk_length;

            const bool at_boundary = chunk_length >= options.chunk_min &&
                (((rolling & mask) == 0U) || chunk_length >= options.chunk_max);
            if (at_boundary) {
                info.chunks.push_back(finish_chunk(chunk_hash, chunk_length));
                rolling = 0;
                chunk_hash = kFnvOffset;
                chunk_length = 0;
            }
        }
    }
    if (input.bad()) {
        throw std::runtime_error("read failed");
    }
    if (chunk_length != 0) {
        info.chunks.push_back(finish_chunk(chunk_hash, chunk_length));
    }
    return info;
}

bool byte_equal(const std::filesystem::path& left, const std::filesystem::path& right) {
    std::ifstream a(left, std::ios::binary);
    std::ifstream b(right, std::ios::binary);
    if (!a || !b) {
        return false;
    }
    std::array<char, 64 * 1024> left_buffer{};
    std::array<char, 64 * 1024> right_buffer{};
    while (a && b) {
        a.read(left_buffer.data(), static_cast<std::streamsize>(left_buffer.size()));
        b.read(right_buffer.data(), static_cast<std::streamsize>(right_buffer.size()));
        const auto a_count = a.gcount();
        const auto b_count = b.gcount();
        if (a_count != b_count || !std::equal(left_buffer.begin(), left_buffer.begin() + a_count,
                                               right_buffer.begin())) {
            return false;
        }
    }
    return !a.bad() && !b.bad();
}

std::uint64_t pair_key(std::size_t left, std::size_t right) {
    const auto a = static_cast<std::uint64_t>(std::min(left, right));
    const auto b = static_cast<std::uint64_t>(std::max(left, right));
    return (a << 32U) | b;
}

struct PostingRecord {
    std::uint64_t chunk = 0;
    std::uint32_t file = 0;
};

void accumulate_owner_pairs(const std::vector<std::size_t>& owners,
                            std::unordered_map<std::uint64_t, std::size_t>& shared_counts) {
    for (std::size_t i = 0; i < owners.size(); ++i) {
        for (std::size_t j = i + 1; j < owners.size(); ++j) {
            ++shared_counts[pair_key(owners[i], owners[j])];
        }
    }
}

class TemporaryIndexDirectory {
public:
    explicit TemporaryIndexDirectory(const std::filesystem::path& requested_parent) {
        std::error_code error;
        const auto parent = requested_parent.empty() ? std::filesystem::temp_directory_path(error)
                                                      : requested_parent;
        if (error) throw std::runtime_error("cannot locate temporary directory: " + error.message());
        std::filesystem::create_directories(parent, error);
        if (error) throw std::runtime_error("cannot create temporary parent: " + error.message());
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        for (std::size_t attempt = 0; attempt < 100; ++attempt) {
            path_ = parent / ("bytebraid-index-" + std::to_string(stamp) + "-" +
                              std::to_string(attempt));
            if (std::filesystem::create_directory(path_, error)) return;
            if (error && error != std::errc::file_exists) {
                throw std::runtime_error("cannot create temporary index: " + error.message());
            }
            error.clear();
        }
        throw std::runtime_error("cannot allocate a unique temporary index directory");
    }

    TemporaryIndexDirectory(const TemporaryIndexDirectory&) = delete;
    TemporaryIndexDirectory& operator=(const TemporaryIndexDirectory&) = delete;

    ~TemporaryIndexDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

std::filesystem::path partition_path(const std::filesystem::path& root, std::size_t partition) {
    return root / ("partition-" + std::to_string(partition) + ".bin");
}

void append_records(const std::filesystem::path& path, const std::vector<PostingRecord>& records) {
    if (records.empty()) return;
    std::ofstream output(path, std::ios::binary | std::ios::app);
    if (!output) throw std::runtime_error("cannot write temporary index partition");
    for (const auto& record : records) {
        output.write(reinterpret_cast<const char*>(&record.chunk), sizeof(record.chunk));
        output.write(reinterpret_cast<const char*>(&record.file), sizeof(record.file));
    }
    if (!output) throw std::runtime_error("temporary index partition write failed");
}

void build_memory_index(const std::vector<std::unordered_set<std::uint64_t>>& unique_chunks,
                        ScanResult& result,
                        std::unordered_map<std::uint64_t, std::size_t>& shared_counts) {
    std::unordered_map<std::uint64_t, std::vector<std::size_t>> postings;
    for (std::size_t index = 0; index < unique_chunks.size(); ++index) {
        for (const auto chunk : unique_chunks[index]) {
            postings[chunk].push_back(index);
            ++result.index_posting_records;
        }
    }
    result.index_peak_resident_records = result.index_posting_records;
    result.index_partitions_used = result.index_posting_records == 0 ? 0 : 1;
    for (const auto& [chunk, owners] : postings) {
        static_cast<void>(chunk);
        accumulate_owner_pairs(owners, shared_counts);
    }
}

void build_disk_index(const std::vector<std::unordered_set<std::uint64_t>>& unique_chunks,
                      const ScanOptions& options, ScanResult& result,
                      std::unordered_map<std::uint64_t, std::size_t>& shared_counts) {
    if (unique_chunks.size() > std::numeric_limits<std::uint32_t>::max()) {
        throw std::runtime_error("disk index supports at most 2^32 - 1 analyzed paths");
    }
    TemporaryIndexDirectory temporary(options.temp_directory);
    std::vector<std::vector<PostingRecord>> buffers(options.index_partitions);
    std::vector<bool> used(options.index_partitions, false);
    constexpr std::size_t flush_records = 32;
    const auto partition_mask = static_cast<std::uint64_t>(options.index_partitions - 1);
    std::uint64_t buffered_records = 0;

    for (std::size_t index = 0; index < unique_chunks.size(); ++index) {
        for (const auto chunk : unique_chunks[index]) {
            const auto partition = static_cast<std::size_t>(chunk & partition_mask);
            auto& buffer = buffers[partition];
            buffer.push_back({chunk, static_cast<std::uint32_t>(index)});
            ++buffered_records;
            result.index_peak_resident_records = std::max(result.index_peak_resident_records,
                                                           buffered_records);
            used[partition] = true;
            ++result.index_posting_records;
            if (buffer.size() >= flush_records) {
                append_records(partition_path(temporary.path(), partition), buffer);
                buffered_records -= static_cast<std::uint64_t>(buffer.size());
                buffer.clear();
            }
        }
    }
    for (std::size_t partition = 0; partition < buffers.size(); ++partition) {
        append_records(partition_path(temporary.path(), partition), buffers[partition]);
        buffered_records -= static_cast<std::uint64_t>(buffers[partition].size());
        buffers[partition].clear();
        buffers[partition].shrink_to_fit();
    }

    for (std::size_t partition = 0; partition < used.size(); ++partition) {
        if (!used[partition]) continue;
        ++result.index_partitions_used;
        std::ifstream input(partition_path(temporary.path(), partition), std::ios::binary);
        if (!input) throw std::runtime_error("cannot read temporary index partition");
        std::vector<PostingRecord> records;
        PostingRecord record;
        while (input.read(reinterpret_cast<char*>(&record.chunk), sizeof(record.chunk))) {
            if (!input.read(reinterpret_cast<char*>(&record.file), sizeof(record.file))) {
                throw std::runtime_error("truncated temporary index partition");
            }
            records.push_back(record);
        }
        if (!input.eof()) throw std::runtime_error("temporary index partition read failed");
        result.index_peak_resident_records = std::max<std::uint64_t>(
            result.index_peak_resident_records, static_cast<std::uint64_t>(records.size()));
        std::sort(records.begin(), records.end(), [](const PostingRecord& left,
                                                      const PostingRecord& right) {
            if (left.chunk != right.chunk) return left.chunk < right.chunk;
            return left.file < right.file;
        });
        std::size_t begin = 0;
        while (begin < records.size()) {
            std::size_t end = begin + 1;
            while (end < records.size() && records[end].chunk == records[begin].chunk) ++end;
            std::vector<std::size_t> owners;
            owners.reserve(end - begin);
            for (auto index = begin; index < end; ++index) owners.push_back(records[index].file);
            accumulate_owner_pairs(owners, shared_counts);
            begin = end;
        }
    }
}

}  // namespace

ScanResult analyze(const std::filesystem::path& root, const ScanOptions& options) {
    validate(options);
    std::error_code error;
    if (!std::filesystem::exists(root, error) || error) {
        throw std::invalid_argument("scan root does not exist: " + path_text(root));
    }
    if (!std::filesystem::is_directory(root, error) || error) {
        throw std::invalid_argument("scan root is not a directory: " + path_text(root));
    }

    ScanResult result;
    result.root = std::filesystem::absolute(root, error);
    if (error) {
        result.root = root;
        error.clear();
    }

    const auto flags = std::filesystem::directory_options::skip_permission_denied;
    std::filesystem::recursive_directory_iterator iterator(root, flags, error);
    const std::filesystem::recursive_directory_iterator end;
    if (error) {
        result.warnings.push_back("cannot start directory scan: " + error.message());
        error.clear();
    }

    std::map<IdentityKey, FileInfo> fingerprint_cache;
    std::set<IdentityKey> physical_files_seen;

    while (iterator != end) {
        const auto entry = *iterator;
        const auto status = entry.symlink_status(error);
        if (error) {
            result.warnings.push_back(path_text(entry.path()) + ": " + error.message());
            error.clear();
        } else if (std::filesystem::is_symlink(status)) {
            if (std::filesystem::is_directory(status)) {
                iterator.disable_recursion_pending();
            }
        } else if (std::filesystem::is_regular_file(status)) {
            const auto size = entry.file_size(error);
            if (error) {
                result.warnings.push_back(path_text(entry.path()) + ": " + error.message());
                error.clear();
            } else {
                ++result.files_seen;
                result.bytes_seen += static_cast<std::uint64_t>(size);
                if (size >= options.min_size) {
                    try {
                        const auto identity = read_physical_identity(entry.path());
                        FileInfo info;
                        if (identity.available) {
                            const auto key = identity_key(identity);
                            const auto cached = fingerprint_cache.find(key);
                            if (cached != fingerprint_cache.end() && cached->second.size == size) {
                                info = cached->second;
                                info.path = entry.path();
                                info.identity = identity;
                            } else {
                                info = fingerprint_file(entry.path(), size, options, identity);
                                fingerprint_cache[key] = info;
                            }
                            if (physical_files_seen.insert(key).second) {
                                ++result.physical_eligible_files;
                                result.physical_eligible_bytes += static_cast<std::uint64_t>(size);
                            }
                        } else {
                            info = fingerprint_file(entry.path(), size, options, identity);
                            ++result.physical_eligible_files;
                            result.physical_eligible_bytes += static_cast<std::uint64_t>(size);
                            ++result.identity_unavailable_files;
                        }
                        result.files.push_back(std::move(info));
                        ++result.eligible_files;
                    } catch (const std::exception& exception) {
                        result.warnings.push_back(path_text(entry.path()) + ": " + exception.what());
                    }
                }
            }
        }
        iterator.increment(error);
        if (error) {
            result.warnings.push_back("directory traversal: " + error.message());
            error.clear();
        }
    }

    std::sort(result.files.begin(), result.files.end(), [](const FileInfo& a, const FileInfo& b) {
        return path_text(a.path) < path_text(b.path);
    });

    std::map<IdentityKey, std::vector<std::size_t>> identity_groups;
    for (std::size_t index = 0; index < result.files.size(); ++index) {
        if (result.files[index].identity.available) {
            identity_groups[identity_key(result.files[index].identity)].push_back(index);
        }
    }
    for (const auto& [identity, members] : identity_groups) {
        static_cast<void>(identity);
        if (members.size() < 2) continue;
        HardLinkGroup group;
        group.file_size = result.files[members.front()].size;
        for (const auto index : members) {
            group.paths.push_back(result.files[index].path);
            group.reported_links = std::max(group.reported_links,
                                             result.files[index].identity.reported_links);
        }
        result.hard_link_groups.push_back(std::move(group));
    }
    std::sort(result.hard_link_groups.begin(), result.hard_link_groups.end(),
              [](const HardLinkGroup& left, const HardLinkGroup& right) {
        if (left.file_size != right.file_size) return left.file_size > right.file_size;
        return path_text(left.paths.front()) < path_text(right.paths.front());
    });

    std::map<std::pair<std::uintmax_t, std::uint64_t>, std::vector<std::size_t>> hash_groups;
    for (std::size_t index = 0; index < result.files.size(); ++index) {
        hash_groups[{result.files[index].size, result.files[index].whole_hash}].push_back(index);
    }

    std::unordered_set<std::uint64_t> exact_pairs;
    for (const auto& [signature, candidates] : hash_groups) {
        if (candidates.size() < 2) {
            continue;
        }
        std::vector<std::vector<std::size_t>> confirmed;
        for (const auto candidate : candidates) {
            bool placed = false;
            for (auto& group : confirmed) {
                if (byte_equal(result.files[candidate].path, result.files[group.front()].path)) {
                    group.push_back(candidate);
                    placed = true;
                    break;
                }
            }
            if (!placed) {
                confirmed.push_back({candidate});
            }
        }
        for (const auto& group : confirmed) {
            if (group.size() < 2) {
                continue;
            }
            for (std::size_t i = 0; i < group.size(); ++i) {
                for (std::size_t j = i + 1; j < group.size(); ++j) {
                    exact_pairs.insert(pair_key(group[i], group[j]));
                }
            }
            std::set<IdentityKey> physical_identities;
            std::size_t identity_unavailable = 0;
            for (const auto index : group) {
                if (result.files[index].identity.available) {
                    physical_identities.insert(identity_key(result.files[index].identity));
                } else {
                    ++identity_unavailable;
                }
            }
            const auto identity_complete = identity_unavailable == 0;
            const auto physical_copies = physical_identities.size();
            if (identity_complete && physical_copies < 2) continue;
            ExactGroup exact;
            exact.file_size = signature.first;
            exact.physical_copies = physical_copies;
            exact.physical_identity_complete = identity_complete;
            if (physical_copies > 1) {
                exact.reclaimable_bytes = static_cast<std::uint64_t>(exact.file_size) *
                                          static_cast<std::uint64_t>(physical_copies - 1);
            }
            for (const auto index : group) {
                exact.paths.push_back(result.files[index].path);
            }
            result.reclaimable_exact_bytes += exact.reclaimable_bytes;
            result.exact_groups.push_back(std::move(exact));
        }
    }

    std::sort(result.exact_groups.begin(), result.exact_groups.end(), [](const ExactGroup& a,
                                                                         const ExactGroup& b) {
        const auto a_waste = a.reclaimable_bytes;
        const auto b_waste = b.reclaimable_bytes;
        if (a_waste != b_waste) return a_waste > b_waste;
        return path_text(a.paths.front()) < path_text(b.paths.front());
    });

    std::vector<std::unordered_set<std::uint64_t>> unique_chunks(result.files.size());
    std::set<IdentityKey> indexed_physical_files;
    for (std::size_t index = 0; index < result.files.size(); ++index) {
        if (result.files[index].identity.available &&
            !indexed_physical_files.insert(identity_key(result.files[index].identity)).second) {
            continue;
        }
        unique_chunks[index].insert(result.files[index].chunks.begin(), result.files[index].chunks.end());
    }

    std::unordered_map<std::uint64_t, std::size_t> shared_counts;
    if (options.index_backend == IndexBackend::memory) {
        build_memory_index(unique_chunks, result, shared_counts);
    } else {
        build_disk_index(unique_chunks, options, result, shared_counts);
    }

    for (const auto& [key, shared] : shared_counts) {
        if (exact_pairs.contains(key)) {
            continue;
        }
        const auto left_index = static_cast<std::size_t>(key >> 32U);
        const auto right_index = static_cast<std::size_t>(key & 0xffffffffULL);
        const auto union_count = unique_chunks[left_index].size() + unique_chunks[right_index].size() - shared;
        if (union_count == 0) {
            continue;
        }
        const auto similarity = static_cast<double>(shared) / static_cast<double>(union_count);
        if (similarity >= options.similarity_threshold) {
            result.near_pairs.push_back({result.files[left_index].path, result.files[right_index].path,
                                         similarity, shared, union_count});
        }
    }
    std::sort(result.near_pairs.begin(), result.near_pairs.end(), [](const NearPair& a,
                                                                     const NearPair& b) {
        if (std::abs(a.similarity - b.similarity) > std::numeric_limits<double>::epsilon()) {
            return a.similarity > b.similarity;
        }
        if (path_text(a.left) != path_text(b.left)) return path_text(a.left) < path_text(b.left);
        return path_text(a.right) < path_text(b.right);
    });
    return result;
}

std::string format_bytes(std::uint64_t bytes) {
    constexpr std::array<const char*, 5> units{"B", "KiB", "MiB", "GiB", "TiB"};
    double value = static_cast<double>(bytes);
    std::size_t unit = 0;
    while (value >= 1024.0 && unit + 1 < units.size()) {
        value /= 1024.0;
        ++unit;
    }
    std::ostringstream out;
    out << std::fixed << std::setprecision(unit == 0 ? 0 : 2) << value << ' ' << units[unit];
    return out.str();
}

std::string index_backend_name(IndexBackend backend) {
    return backend == IndexBackend::disk ? "disk" : "memory";
}

std::string to_json(const ScanResult& result, const ScanOptions& options) {
    std::ostringstream out;
    out << "{\n  \"schema_version\": 2,\n"
        << "  \"root\": \"" << json_escape(path_text(result.root)) << "\",\n"
        << "  \"options\": {\"min_size\": " << options.min_size
        << ", \"chunk_min\": " << options.chunk_min
        << ", \"chunk_average\": " << options.chunk_average
        << ", \"chunk_max\": " << options.chunk_max
        << ", \"similarity_threshold\": " << std::fixed << std::setprecision(4)
        << options.similarity_threshold
        << ", \"index_backend\": \"" << index_backend_name(options.index_backend)
        << "\", \"index_partitions\": " << options.index_partitions << "},\n"
        << "  \"summary\": {\"files_seen\": " << result.files_seen
        << ", \"bytes_seen\": " << result.bytes_seen
        << ", \"eligible_files\": " << result.eligible_files
        << ", \"physical_eligible_files\": " << result.physical_eligible_files
        << ", \"physical_eligible_bytes\": " << result.physical_eligible_bytes
        << ", \"identity_unavailable_files\": " << result.identity_unavailable_files
        << ", \"hard_link_groups\": " << result.hard_link_groups.size()
        << ", \"exact_groups\": " << result.exact_groups.size()
        << ", \"near_pairs\": " << result.near_pairs.size()
        << ", \"reclaimable_exact_bytes\": " << result.reclaimable_exact_bytes
        << ", \"index_posting_records\": " << result.index_posting_records
        << ", \"index_peak_resident_records\": " << result.index_peak_resident_records
        << ", \"index_partitions_used\": " << result.index_partitions_used << "},\n"
        << "  \"hard_link_groups\": [";
    for (std::size_t i = 0; i < result.hard_link_groups.size(); ++i) {
        const auto& group = result.hard_link_groups[i];
        out << (i == 0 ? "\n" : ",\n") << "    {\"file_size\": " << group.file_size
            << ", \"reported_links\": " << group.reported_links << ", \"paths\": [";
        for (std::size_t j = 0; j < group.paths.size(); ++j) {
            out << (j == 0 ? "" : ", ") << "\"" << json_escape(path_text(group.paths[j])) << "\"";
        }
        out << "]}";
    }
    out << (result.hard_link_groups.empty() ? "],\n" : "\n  ],\n")
        << "  \"exact_groups\": [";
    for (std::size_t i = 0; i < result.exact_groups.size(); ++i) {
        const auto& group = result.exact_groups[i];
        out << (i == 0 ? "\n" : ",\n") << "    {\"file_size\": " << group.file_size
            << ", \"physical_copies\": " << group.physical_copies
            << ", \"physical_identity_complete\": "
            << (group.physical_identity_complete ? "true" : "false")
            << ", \"reclaimable_bytes\": " << group.reclaimable_bytes << ", \"paths\": [";
        for (std::size_t j = 0; j < group.paths.size(); ++j) {
            out << (j == 0 ? "" : ", ") << "\"" << json_escape(path_text(group.paths[j])) << "\"";
        }
        out << "]}";
    }
    out << (result.exact_groups.empty() ? "],\n" : "\n  ],\n") << "  \"near_pairs\": [";
    for (std::size_t i = 0; i < result.near_pairs.size(); ++i) {
        const auto& pair = result.near_pairs[i];
        out << (i == 0 ? "\n" : ",\n")
            << "    {\"left\": \"" << json_escape(path_text(pair.left))
            << "\", \"right\": \"" << json_escape(path_text(pair.right))
            << "\", \"similarity\": " << std::fixed << std::setprecision(6) << pair.similarity
            << ", \"shared_chunks\": " << pair.shared_chunks
            << ", \"union_chunks\": " << pair.union_chunks << "}";
    }
    out << (result.near_pairs.empty() ? "],\n" : "\n  ],\n") << "  \"warnings\": [";
    for (std::size_t i = 0; i < result.warnings.size(); ++i) {
        out << (i == 0 ? "" : ", ") << "\"" << json_escape(result.warnings[i]) << "\"";
    }
    out << "]\n}\n";
    return out.str();
}

}  // namespace bytebraid
