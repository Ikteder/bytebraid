#include "bytebraid/analyzer.hpp"

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;

namespace {

void expect(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}
void write_binary(const fs::path& path, const std::string& data) {
    std::ofstream output(path, std::ios::binary);
    if (!output) throw std::runtime_error("fixture write failed");
    output.write(data.data(), static_cast<std::streamsize>(data.size()));
}

struct TempDirectory {
    fs::path path;
    TempDirectory() {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path = fs::temp_directory_path() / ("bytebraid-test-" + std::to_string(stamp));
        fs::create_directories(path);
    }
    ~TempDirectory() {
        std::error_code error;
        fs::remove_all(path, error);
    }
};

void test_exact_and_near_detection() {
    TempDirectory temp;
    const std::string a(128, 'A');
    const std::string b(128, 'B');
    const std::string c(128, 'C');
    const std::string d(128, 'D');
    const auto base = a + b + c + d;
    write_binary(temp.path / "base.bin", base);
    write_binary(temp.path / "base-copy.bin", base);
    write_binary(temp.path / "revision.bin", a + b + std::string(128, 'X') + d);
    write_binary(temp.path / "unrelated.bin", std::string(128, 'E') + std::string(128, 'F') +
                                               std::string(128, 'G') + std::string(128, 'H'));
    write_binary(temp.path / "tiny.txt", "tiny");

    bytebraid::ScanOptions options;
    options.min_size = 64;
    options.chunk_min = 64;
    options.chunk_average = 128;
    options.chunk_max = 128;
    options.similarity_threshold = 0.59;
    const auto result = bytebraid::analyze(temp.path, options);

    expect(result.files_seen == 5, "all regular files should be observed");
    expect(result.eligible_files == 4, "minimum size should filter one file");
    expect(result.exact_groups.size() == 1, "one exact group expected");
    expect(result.exact_groups[0].paths.size() == 2, "exact group should contain both copies");
    expect(result.reclaimable_exact_bytes == 512, "one 512-byte copy is reclaimable");
    expect(result.near_pairs.size() == 2, "revision should pair with both exact copies");
    for (const auto& pair : result.near_pairs) {
        expect(std::abs(pair.similarity - 0.6) < 0.000001, "expected 3/5 Jaccard score");
        expect(pair.shared_chunks == 3 && pair.union_chunks == 5, "chunk evidence should be explicit");
    }
    expect(result.warnings.empty(), "controlled fixture should have no warnings");
    expect(result.physical_eligible_files == 4, "ordinary files should have distinct identities");

    const auto json = bytebraid::to_json(result, options);
    expect(json.find("\"schema_version\": 2") != std::string::npos, "JSON schema should be version 2");
    expect(json.find("\"exact_groups\": 1") != std::string::npos, "JSON summary should include exact count");
    expect(json.find("\"similarity\": 0.600000") != std::string::npos, "JSON should include score");

    auto disk_options = options;
    disk_options.index_backend = bytebraid::IndexBackend::disk;
    disk_options.index_partitions = 8;
    disk_options.temp_directory = temp.path / "spill-parent";
    const auto disk_result = bytebraid::analyze(temp.path, disk_options);
    expect(disk_result.exact_groups.size() == result.exact_groups.size(),
           "disk index must preserve exact groups");
    expect(disk_result.near_pairs.size() == result.near_pairs.size(),
           "disk index must preserve near pairs");
    expect(disk_result.reclaimable_exact_bytes == result.reclaimable_exact_bytes,
           "disk index must preserve reclaimable accounting");
    expect(disk_result.index_posting_records == result.index_posting_records,
           "both indexes must process the same posting records");
    expect(disk_result.index_partitions_used > 0 &&
           disk_result.index_peak_resident_records <= disk_result.index_posting_records,
           "disk index must expose bounded partition evidence");
}

void test_hard_link_accounting() {
    TempDirectory temp;
    const std::string content(1024, 'Q');
    const auto original = temp.path / "original.bin";
    const auto alias = temp.path / "alias.bin";
    const auto copy = temp.path / "copy.bin";
    write_binary(original, content);
    fs::create_hard_link(original, alias);
    write_binary(copy, content);

    const auto result = bytebraid::analyze(temp.path);
    expect(result.eligible_files == 3, "three logical paths should be analyzed");
    expect(result.physical_eligible_files == 2, "hard-linked aliases are one physical file");
    expect(result.physical_eligible_bytes == 2048, "physical bytes should not count the alias twice");
    expect(result.hard_link_groups.size() == 1, "one hard-link group expected");
    expect(result.hard_link_groups[0].paths.size() == 2, "hard-link group should list both aliases");
    expect(result.exact_groups.size() == 1, "the separate copy should form an exact group");
    expect(result.exact_groups[0].paths.size() == 3, "exact evidence should retain all logical paths");
    expect(result.exact_groups[0].physical_copies == 2, "exact group should count physical copies");
    expect(result.reclaimable_exact_bytes == 1024, "only one physical copy is reclaimable");

    TempDirectory links_only;
    write_binary(links_only.path / "one.bin", content);
    fs::create_hard_link(links_only.path / "one.bin", links_only.path / "two.bin");
    const auto links_only_result = bytebraid::analyze(links_only.path);
    expect(links_only_result.hard_link_groups.size() == 1, "hard-link-only group should be visible");
    expect(links_only_result.exact_groups.empty(), "aliases alone are not duplicate physical copies");
    expect(links_only_result.reclaimable_exact_bytes == 0, "aliases alone reclaim no file data");

    const auto json = bytebraid::to_json(result, {});
    expect(json.find("\"hard_link_groups\": 1") != std::string::npos,
           "JSON summary should include hard-link count");
    expect(json.find("\"physical_copies\": 2") != std::string::npos,
           "JSON exact evidence should include physical copies");
}

void test_empty_directory() {
    TempDirectory temp;
    const auto result = bytebraid::analyze(temp.path);
    expect(result.files_seen == 0, "empty directory should scan cleanly");
    expect(result.exact_groups.empty() && result.near_pairs.empty(), "empty directory has no matches");
}

void test_invalid_inputs() {
    TempDirectory temp;
    auto options = bytebraid::ScanOptions{};
    options.chunk_average = 1000;
    bool bad_chunk_rejected = false;
    try { static_cast<void>(bytebraid::analyze(temp.path, options)); }
    catch (const std::invalid_argument&) { bad_chunk_rejected = true; }
    expect(bad_chunk_rejected, "non-power-of-two average must be rejected");

    options = bytebraid::ScanOptions{};
    options.index_partitions = 3;
    bool bad_partitions_rejected = false;
    try { static_cast<void>(bytebraid::analyze(temp.path, options)); }
    catch (const std::invalid_argument&) { bad_partitions_rejected = true; }
    expect(bad_partitions_rejected, "non-power-of-two partition count must be rejected");

    bool missing_rejected = false;
    try { static_cast<void>(bytebraid::analyze(temp.path / "missing")); }
    catch (const std::invalid_argument&) { missing_rejected = true; }
    expect(missing_rejected, "missing root must be rejected");
}

}  // namespace

int main() {
    try {
        test_exact_and_near_detection();
        std::cout << "PASS exact, near, and backend equivalence\n";
        test_hard_link_accounting();
        std::cout << "PASS hard-link physical accounting\n";
        test_empty_directory();
        std::cout << "PASS empty directory\n";
        test_invalid_inputs();
        std::cout << "PASS invalid inputs\n";
        std::cout << "4/4 tests passed\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "FAIL " << exception.what() << '\n';
        return 1;
    }
}
