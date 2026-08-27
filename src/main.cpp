#include "bytebraid/analyzer.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void usage(std::ostream& out) {
    out << "ByteBraid 0.1.0 - read-only duplicate and version-family discovery\n\n"
        << "Usage:\n"
        << "  bytebraid scan <directory> [options]\n\n"
        << "Options:\n"
        << "  --min-size <bytes>       Ignore smaller files (default: 1)\n"
        << "  --similarity <0..1>      Near-match Jaccard threshold (default: 0.65)\n"
        << "  --chunk-min <bytes>      Minimum content chunk (default: 2048)\n"
        << "  --chunk-average <bytes>  Power-of-two target chunk (default: 8192)\n"
        << "  --chunk-max <bytes>      Maximum content chunk (default: 32768)\n"
        << "  --json <path|->          Write machine-readable report; '-' suppresses human output\n"
        << "  --help                   Show this help\n\n"
        << "ByteBraid never deletes or modifies scanned files.\n";
}

std::string require_value(int& index, int argc, char** argv, const std::string& option) {
    if (++index >= argc) {
        throw std::invalid_argument("missing value for " + option);
    }
    return argv[index];
}

std::uint64_t parse_integer(const std::string& text, const std::string& option) {
    if (text.empty() || text.front() == '-') {
        throw std::invalid_argument("invalid integer for " + option + ": " + text);
    }
    std::size_t consumed = 0;
    const auto value = std::stoull(text, &consumed);
    if (consumed != text.size()) {
        throw std::invalid_argument("invalid integer for " + option + ": " + text);
    }
    return value;
}

void print_human(const bytebraid::ScanResult& result, const bytebraid::ScanOptions& options) {
    std::cout << "ByteBraid scan\n"
              << "  root:       " << result.root.string() << '\n'
              << "  observed:   " << result.files_seen << " files / "
              << bytebraid::format_bytes(result.bytes_seen) << '\n'
              << "  analyzed:   " << result.eligible_files << " files >= "
              << bytebraid::format_bytes(options.min_size) << '\n'
              << "  exact:      " << result.exact_groups.size() << " groups / "
              << bytebraid::format_bytes(result.reclaimable_exact_bytes)
              << " safely identifiable copies\n"
              << "  near:       " << result.near_pairs.size() << " pairs >= "
              << std::fixed << std::setprecision(0) << options.similarity_threshold * 100.0 << "%\n";

    for (std::size_t index = 0; index < result.exact_groups.size(); ++index) {
        const auto& group = result.exact_groups[index];
        std::cout << "\nExact group " << index + 1 << " (" << bytebraid::format_bytes(group.file_size)
                  << " each):\n";
        for (const auto& path : group.paths) {
            std::cout << "  = " << path.string() << '\n';
        }
    }
    for (const auto& pair : result.near_pairs) {
        std::cout << "\nNear " << std::setprecision(1) << pair.similarity * 100.0 << "% ("
                  << pair.shared_chunks << '/' << pair.union_chunks << " content chunks)\n"
                  << "  ~ " << pair.left.string() << '\n'
                  << "  ~ " << pair.right.string() << '\n';
    }
    if (!result.warnings.empty()) {
        std::cout << "\nWarnings (" << result.warnings.size() << "):\n";
        for (const auto& warning : result.warnings) {
            std::cout << "  ! " << warning << '\n';
        }
    }
}

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::string(argv[1]) == "--help") {
            usage(std::cout);
            return EXIT_SUCCESS;
        }
        if (argc < 3 || std::string(argv[1]) != "scan") {
            usage(std::cerr);
            return EXIT_FAILURE;
        }

        const std::filesystem::path root = argv[2];
        bytebraid::ScanOptions options;
        std::string json_path;
        for (int index = 3; index < argc; ++index) {
            const std::string option = argv[index];
            if (option == "--min-size") {
                options.min_size = parse_integer(require_value(index, argc, argv, option), option);
            } else if (option == "--similarity") {
                const auto text = require_value(index, argc, argv, option);
                std::size_t consumed = 0;
                options.similarity_threshold = std::stod(text, &consumed);
                if (consumed != text.size()) throw std::invalid_argument("invalid number for " + option);
            } else if (option == "--chunk-min") {
                options.chunk_min = parse_integer(require_value(index, argc, argv, option), option);
            } else if (option == "--chunk-average") {
                options.chunk_average = parse_integer(require_value(index, argc, argv, option), option);
            } else if (option == "--chunk-max") {
                options.chunk_max = parse_integer(require_value(index, argc, argv, option), option);
            } else if (option == "--json") {
                json_path = require_value(index, argc, argv, option);
            } else if (option == "--help") {
                usage(std::cout);
                return EXIT_SUCCESS;
            } else {
                throw std::invalid_argument("unknown option: " + option);
            }
        }

        const auto result = bytebraid::analyze(root, options);
        if (json_path != "-") {
            print_human(result, options);
        }
        if (!json_path.empty()) {
            const auto report = bytebraid::to_json(result, options);
            if (json_path == "-") {
                std::cout << report;
            } else {
                std::ofstream output(json_path, std::ios::binary);
                if (!output) throw std::runtime_error("cannot open JSON report: " + json_path);
                output << report;
                std::cout << "\nJSON report: " << json_path << '\n';
            }
        }
        return EXIT_SUCCESS;
    } catch (const std::exception& exception) {
        std::cerr << "bytebraid: " << exception.what() << '\n';
        return 2;
    }
}
