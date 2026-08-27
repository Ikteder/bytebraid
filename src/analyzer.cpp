#include "bytebraid/analyzer.hpp"

#include <algorithm>
#include <array>
#include <cmath>
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

FileInfo fingerprint_file(const std::filesystem::path& path, std::uintmax_t size,
                          const ScanOptions& options) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("cannot open file");
    }

    FileInfo info;
    info.path = path;
    info.size = size;
    info.whole_hash = kFnvOffset;

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
                        result.files.push_back(fingerprint_file(entry.path(), size, options));
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
            ExactGroup exact;
            exact.file_size = signature.first;
            for (const auto index : group) {
                exact.paths.push_back(result.files[index].path);
            }
            for (std::size_t i = 0; i < group.size(); ++i) {
                for (std::size_t j = i + 1; j < group.size(); ++j) {
                    exact_pairs.insert(pair_key(group[i], group[j]));
                }
            }
            result.reclaimable_exact_bytes += static_cast<std::uint64_t>(exact.file_size) *
                static_cast<std::uint64_t>(exact.paths.size() - 1);
            result.exact_groups.push_back(std::move(exact));
        }
    }

    std::sort(result.exact_groups.begin(), result.exact_groups.end(), [](const ExactGroup& a,
                                                                         const ExactGroup& b) {
        const auto a_waste = a.file_size * (a.paths.size() - 1);
        const auto b_waste = b.file_size * (b.paths.size() - 1);
        if (a_waste != b_waste) return a_waste > b_waste;
        return path_text(a.paths.front()) < path_text(b.paths.front());
    });

    std::vector<std::unordered_set<std::uint64_t>> unique_chunks(result.files.size());
    std::unordered_map<std::uint64_t, std::vector<std::size_t>> postings;
    for (std::size_t index = 0; index < result.files.size(); ++index) {
        unique_chunks[index].insert(result.files[index].chunks.begin(), result.files[index].chunks.end());
        for (const auto chunk : unique_chunks[index]) {
            postings[chunk].push_back(index);
        }
    }

    std::unordered_map<std::uint64_t, std::size_t> shared_counts;
    for (const auto& [chunk, owners] : postings) {
        static_cast<void>(chunk);
        for (std::size_t i = 0; i < owners.size(); ++i) {
            for (std::size_t j = i + 1; j < owners.size(); ++j) {
                ++shared_counts[pair_key(owners[i], owners[j])];
            }
        }
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

std::string to_json(const ScanResult& result, const ScanOptions& options) {
    std::ostringstream out;
    out << "{\n  \"schema_version\": 1,\n"
        << "  \"root\": \"" << json_escape(path_text(result.root)) << "\",\n"
        << "  \"options\": {\"min_size\": " << options.min_size
        << ", \"chunk_min\": " << options.chunk_min
        << ", \"chunk_average\": " << options.chunk_average
        << ", \"chunk_max\": " << options.chunk_max
        << ", \"similarity_threshold\": " << std::fixed << std::setprecision(4)
        << options.similarity_threshold << "},\n"
        << "  \"summary\": {\"files_seen\": " << result.files_seen
        << ", \"bytes_seen\": " << result.bytes_seen
        << ", \"eligible_files\": " << result.eligible_files
        << ", \"exact_groups\": " << result.exact_groups.size()
        << ", \"near_pairs\": " << result.near_pairs.size()
        << ", \"reclaimable_exact_bytes\": " << result.reclaimable_exact_bytes << "},\n"
        << "  \"exact_groups\": [";
    for (std::size_t i = 0; i < result.exact_groups.size(); ++i) {
        const auto& group = result.exact_groups[i];
        out << (i == 0 ? "\n" : ",\n") << "    {\"file_size\": " << group.file_size << ", \"paths\": [";
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
