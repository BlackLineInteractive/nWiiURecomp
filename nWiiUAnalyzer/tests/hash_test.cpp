#include "nwiiu/analyzer/hash.h"
#include "test_support.h"
#include <array>
#include <fstream>

int main() {
    test::TempDir temp;
    const auto empty = temp.path() / "empty.bin";
    std::ofstream(empty, std::ios::binary).close();
    test::require(
        nwiiu::analyzer::sha256_file(empty) ==
            "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
        "empty SHA-256");

    const auto abc = temp.path() / "abc.bin";
    const std::array<uint8_t, 3> bytes{'a', 'b', 'c'};
    test::write_bytes(abc, std::as_bytes(std::span(bytes)));
    test::require(
        nwiiu::analyzer::sha256_file(abc) ==
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
        "abc SHA-256");
    test::require(
        nwiiu::analyzer::sha256(bytes) ==
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
        "span SHA-256");

    test::require_throws(
        [&] { nwiiu::analyzer::sha256_file(temp.path() / "missing"); },
        "cannot open input", "missing file");
}
