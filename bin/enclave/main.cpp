// bin/enclave/main.cpp
// straylight-enclave — SGX secure inference tool
// Usage: straylight-enclave <attest|seal|unseal|infer> [options]

#include "attestation.h"
#include "sealed_storage.h"
#include "secure_inference.h"

#include <straylight/error.h>
#include <straylight/result.h>

#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <unistd.h>

using namespace straylight;
using namespace straylight::enclave;

static void print_usage(const char* prog) {
    std::cerr << "Usage: " << prog << " <command> [options]\n\n"
              << "Commands:\n"
              << "  attest   [--user-data <hex>]\n"
              << "  seal     --input <file> --output <file> [--policy <mrenclave|mrsigner>]\n"
              << "  unseal   --input <file> --output <file>\n"
              << "  infer    --model <sealed-model> --input <comma-sep-floats>\n";
}

static std::string find_arg(int argc, char* argv[], const std::string& flag) {
    for (int i = 0; i < argc - 1; ++i) {
        if (argv[i] == flag) return argv[i + 1];
    }
    return "";
}

static std::string to_hex(const uint8_t* data, size_t len) {
    std::ostringstream oss;
    for (size_t i = 0; i < len; ++i) {
        oss << std::hex << std::setfill('0') << std::setw(2)
            << static_cast<int>(data[i]);
    }
    return oss.str();
}

static std::vector<uint8_t> read_file(const std::string& path) {
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) return {};
    return std::vector<uint8_t>(
        std::istreambuf_iterator<char>(ifs),
        std::istreambuf_iterator<char>());
}

static bool write_file(const std::string& path, const std::vector<uint8_t>& data) {
    std::ofstream ofs(path, std::ios::binary);
    if (!ofs) return false;
    ofs.write(reinterpret_cast<const char*>(data.data()),
              static_cast<std::streamsize>(data.size()));
    return ofs.good();
}

static Result<void, SLError> cmd_attest(int argc, char* argv[]) {
    std::string user_data_str = find_arg(argc, argv, "--user-data");

    std::vector<uint8_t> user_data;
    if (!user_data_str.empty()) {
        // Parse hex string.
        for (size_t i = 0; i + 1 < user_data_str.size(); i += 2) {
            uint8_t byte = static_cast<uint8_t>(
                std::stoul(user_data_str.substr(i, 2), nullptr, 16));
            user_data.push_back(byte);
        }
    }

    Attestation attest;
    auto report_result = attest.create_report(user_data);
    if (!report_result.has_value()) {
        return Result<void, SLError>::error(
            {SLErrorCode::Internal, report_result.error()});
    }

    const auto& report = report_result.value();
    std::cout << "SGX Attestation Report:\n";
    std::cout << "  MRENCLAVE: " << to_hex(report.mr_enclave.data(), 32) << "\n";
    std::cout << "  MRSIGNER:  " << to_hex(report.mr_signer.data(), 32) << "\n";
    std::cout << "  ISV ProdID: " << report.isv_prod_id << "\n";
    std::cout << "  ISV SVN:    " << report.isv_svn << "\n";

    // Verify.
    auto verify_result = attest.verify_report(report);
    if (!verify_result.has_value()) {
        return Result<void, SLError>::error(
            {SLErrorCode::Internal, verify_result.error()});
    }
    std::cout << "  Verified: " << (verify_result.value() ? "YES" : "NO") << "\n";

    // Generate quote.
    auto quote_result = attest.get_quote(report);
    if (!quote_result.has_value()) {
        return Result<void, SLError>::error(
            {SLErrorCode::Internal, quote_result.error()});
    }
    std::cout << "  Quote version: " << quote_result.value().version << "\n";
    std::cout << "  Quote signature: "
              << to_hex(quote_result.value().signature.data(),
                        std::min(quote_result.value().signature.size(), static_cast<size_t>(16)))
              << "...\n";

    return Result<void, SLError>::ok();
}

static Result<void, SLError> cmd_seal(int argc, char* argv[]) {
    std::string input_path = find_arg(argc, argv, "--input");
    std::string output_path = find_arg(argc, argv, "--output");
    std::string policy_str = find_arg(argc, argv, "--policy");

    if (input_path.empty() || output_path.empty()) {
        return Result<void, SLError>::error(
            {SLErrorCode::InvalidArgument, "seal requires --input and --output"});
    }

    SealPolicy policy = SealPolicy::MRSigner;
    if (policy_str == "mrenclave") policy = SealPolicy::MREnclave;

    auto data = read_file(input_path);
    if (data.empty()) {
        return Result<void, SLError>::error(
            {SLErrorCode::IOError, "Cannot read input file: " + input_path});
    }

    SealedStorage storage;
    auto sealed = storage.seal(data, policy);
    if (!sealed.has_value()) {
        return Result<void, SLError>::error({SLErrorCode::Internal, sealed.error()});
    }

    if (!write_file(output_path, sealed.value())) {
        return Result<void, SLError>::error(
            {SLErrorCode::IOError, "Cannot write output file: " + output_path});
    }

    std::cout << "Sealed " << data.size() << " bytes -> " << sealed.value().size()
              << " bytes (policy=" << (policy == SealPolicy::MREnclave ? "MRENCLAVE" : "MRSIGNER")
              << ")\n";
    std::cout << "Written to " << output_path << "\n";

    return Result<void, SLError>::ok();
}

static Result<void, SLError> cmd_unseal(int argc, char* argv[]) {
    std::string input_path = find_arg(argc, argv, "--input");
    std::string output_path = find_arg(argc, argv, "--output");

    if (input_path.empty() || output_path.empty()) {
        return Result<void, SLError>::error(
            {SLErrorCode::InvalidArgument, "unseal requires --input and --output"});
    }

    auto sealed = read_file(input_path);
    if (sealed.empty()) {
        return Result<void, SLError>::error(
            {SLErrorCode::IOError, "Cannot read sealed file: " + input_path});
    }

    SealedStorage storage;
    auto data = storage.unseal(sealed);
    if (!data.has_value()) {
        return Result<void, SLError>::error({SLErrorCode::Internal, data.error()});
    }

    if (!write_file(output_path, data.value())) {
        return Result<void, SLError>::error(
            {SLErrorCode::IOError, "Cannot write output file: " + output_path});
    }

    std::cout << "Unsealed " << sealed.size() << " bytes -> " << data.value().size()
              << " bytes\n";
    std::cout << "Written to " << output_path << "\n";

    return Result<void, SLError>::ok();
}

static Result<void, SLError> cmd_infer(int argc, char* argv[]) {
    std::string model_path = find_arg(argc, argv, "--model");
    std::string input_str = find_arg(argc, argv, "--input");

    if (model_path.empty()) {
        return Result<void, SLError>::error(
            {SLErrorCode::InvalidArgument, "infer requires --model"});
    }

    // Parse comma-separated floats.
    std::vector<float> input;
    if (!input_str.empty()) {
        std::istringstream iss(input_str);
        std::string token;
        while (std::getline(iss, token, ',')) {
            input.push_back(std::stof(token));
        }
    } else {
        // Default test input.
        input = {1.0f, 0.5f, -0.3f, 0.8f};
    }

    SecureInference inference;
    auto load = inference.load_model(model_path);
    if (!load.has_value()) {
        return Result<void, SLError>::error({SLErrorCode::Internal, load.error()});
    }

    auto output = inference.infer(input);
    if (!output.has_value()) {
        return Result<void, SLError>::error({SLErrorCode::Internal, output.error()});
    }

    std::cout << "Secure inference result (" << output.value().size() << " outputs):\n";
    for (size_t i = 0; i < output.value().size(); ++i) {
        std::cout << "  [" << i << "] = " << output.value()[i] << "\n";
    }

    return Result<void, SLError>::ok();
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    std::string cmd = argv[1];
    Result<void, SLError> result = Result<void, SLError>::ok();

    if (cmd == "attest") {
        result = cmd_attest(argc, argv);
    } else if (cmd == "seal") {
        result = cmd_seal(argc, argv);
    } else if (cmd == "unseal") {
        result = cmd_unseal(argc, argv);
    } else if (cmd == "infer") {
        result = cmd_infer(argc, argv);
    } else if (cmd == "--help" || cmd == "-h") {
        print_usage(argv[0]);
        return 0;
    } else {
        std::cerr << "Unknown command: " << cmd << "\n";
        print_usage(argv[0]);
        return 1;
    }

    if (!result.has_value()) {
        std::cerr << "Error [" << static_cast<int>(result.error().code())
                  << "]: " << result.error().message() << "\n";
        return static_cast<int>(result.error().code());
    }

    return 0;
}
