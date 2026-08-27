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

    const auto json = bytebraid::to_json(result, options);
    expect(json.find("\"exact_groups\": 1") != std::string::npos, "JSON summary should include exact count");
    expect(json.find("\"similarity\": 0.600000") != std::string::npos, "JSON should include score");
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

    bool missing_rejected = false;
    try { static_cast<void>(bytebraid::analyze(temp.path / "missing")); }
    catch (const std::invalid_argument&) { missing_rejected = true; }
    expect(missing_rejected, "missing root must be rejected");
}

}  // namespace

int main() {
    try {
        test_exact_and_near_detection();
        std::cout << "PASS exact and near detection\n";
        test_empty_directory();
        std::cout << "PASS empty directory\n";
        test_invalid_inputs();
        std::cout << "PASS invalid inputs\n";
        std::cout << "3/3 tests passed\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "FAIL " << exception.what() << '\n';
        return 1;
    }
}
