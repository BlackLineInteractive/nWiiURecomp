#include "nwiiu/analyzer/analysis.h"
#include "nwiiu/analyzer/ppc_instruction.h"
#include "nwiiu/analyzer/hash.h"
#include "nwiiu/analyzer/rpx.h"
#include "test_support.h"
#include <cstdint>
#include <initializer_list>
#include <string>

namespace {
uint32_t instruction_at(const nwiiu::analyzer::RpxImage& image,
                        uint32_t address) {
    for (const auto& section : image.sections) {
        if (!section.executable() || address < section.address) {
            continue;
        }
        const size_t offset = address - section.address;
        if (offset + sizeof(uint32_t) <= section.data.size()) {
            return static_cast<uint32_t>(section.data[offset]) << 24 |
                   static_cast<uint32_t>(section.data[offset + 1]) << 16 |
                   static_cast<uint32_t>(section.data[offset + 2]) << 8 |
                   static_cast<uint32_t>(section.data[offset + 3]);
        }
    }
    test::require(false, "basic block instruction is executable");
    return 0;
}

void require_basic_blocks(
    const nwiiu::analyzer::Function& function,
    const nwiiu::analyzer::RpxImage& image,
    std::initializer_list<nwiiu::analyzer::BasicBlock> expected) {
    test::require(function.basic_blocks.size() == expected.size(),
                  "basic block count");
    auto expected_block = expected.begin();
    uint32_t covered = 0;
    uint32_t previous_end = 0;
    for (const auto& block : function.basic_blocks) {
        test::require(expected_block != expected.end() &&
                          block.start == expected_block->start &&
                          block.end == expected_block->end &&
                          block.instruction_count ==
                              expected_block->instruction_count,
                      "basic block range");
        test::require((block.start & 3) == 0 && (block.end & 3) == 0 &&
                          block.start < block.end,
                      "basic block aligned half-open range");
        test::require(block.instruction_count ==
                          (block.end - block.start) / sizeof(uint32_t),
                      "basic block instruction count");
        test::require(previous_end == 0 || previous_end <= block.start,
                      "basic blocks ordered and disjoint");
        for (uint32_t address = block.start;
             address + sizeof(uint32_t) < block.end;
             address += sizeof(uint32_t)) {
            const nwiiu::analyzer::PpcInstruction instruction{
                instruction_at(image, address)};
            test::require(
                !instruction.is_direct_branch() &&
                    !instruction.is_branch_to_lr() &&
                    !instruction.is_branch_to_ctr(),
                "control transfer terminates basic block");
        }
        covered += block.instruction_count;
        previous_end = block.end;
        ++expected_block;
    }
    test::require(expected_block == expected.end(),
                  "all expected basic blocks present");
    test::require(covered == function.instruction_count,
                  "basic blocks cover decoded instructions exactly once");
}
}

int main() {
    test::TempDir temp;
    const auto path = temp.path() / "fixture.rpx";
    const auto bytes = test::build_test_rpx();
    test::write_bytes(path, bytes);
    const std::string hash = nwiiu::analyzer::sha256_file(path);
    const nwiiu::analyzer::Target target{"fixture", "fixture", 0, hash,
                                           0x02000000};
    const auto image = nwiiu::analyzer::load_rpx(path, target);

    const auto analysis = nwiiu::analyzer::analyze(image);
    test::require(analysis.functions.size() == 2, "function count");

    const auto entry_it = analysis.functions.find(0x02000000);
    const auto callee_it = analysis.functions.find(0x02000010);
    test::require(entry_it != analysis.functions.end(), "entry function");
    test::require(callee_it != analysis.functions.end(), "callee function");

    const auto& entry = entry_it->second;
    const auto& callee = callee_it->second;
    test::require(entry.start == 0x02000000, "entry start");
    test::require(entry.end == 0x02000010, "entry end");
    test::require(entry.instruction_count == 4, "entry instruction count");
    test::require(entry.callers.empty(), "entry callers");
    test::require(
        entry.callees ==
            std::set<uint32_t>{0x02000010, 0xC0001000},
        "entry keeps local and relocated import callees");
    test::require(entry.discovery_reasons == std::set<std::string>{"entry_point"},
                  "entry discovery reason");
    test::require(entry.dynamic_transfers.size() == 1,
                  "entry dynamic transfer count");
    test::require(entry.dynamic_transfers[0].address == 0x0200000C &&
                      entry.dynamic_transfers[0].kind == "return",
                  "entry return");
    require_basic_blocks(
        entry, image,
        {{0x02000000, 0x02000004, 1}, {0x02000004, 0x02000008, 1},
         {0x02000008, 0x0200000C, 1}, {0x0200000C, 0x02000010, 1}});

    test::require(callee.start == 0x02000010, "callee start");
    test::require(callee.end == 0x02000018, "callee end");
    test::require(callee.instruction_count == 2, "callee instruction count");
    test::require(callee.callers == std::set<uint32_t>{0x02000000},
                  "callee callers");
    test::require(callee.callees.empty(), "callee callees");
    test::require(callee.discovery_reasons ==
                      std::set<std::string>{"direct_call", "relocation"},
                  "callee discovery reasons");
    test::require(callee.dynamic_transfers.size() == 2,
                  "callee dynamic transfer count");
    test::require(callee.dynamic_transfers[0].address == 0x02000010 &&
                      callee.dynamic_transfers[0].kind == "indirect_call",
                  "indirect call");
    test::require(callee.dynamic_transfers[1].address == 0x02000014 &&
                      callee.dynamic_transfers[1].kind == "return",
                  "callee return");
    require_basic_blocks(
        callee, image,
        {{0x02000010, 0x02000014, 1}, {0x02000014, 0x02000018, 1}});

    test::require(entry.jump_table_targets.empty() &&
                      callee.jump_table_targets.empty(),
                  "no jump table targets");
    test::require(analysis.unresolved.empty(), "no unresolved records");
    test::require(!analysis.functions.contains(0xC0001000),
                  "relocated import is not analyzed");

    nwiiu::analyzer::RpxImage jump_table_image;
    jump_table_image.entry_point = 0x02000000;
    auto& jump_code = jump_table_image.sections.emplace_back();
    jump_code.name = ".text";
    jump_code.type = nwiiu::analyzer::kShtProgbits;
    jump_code.flags =
        nwiiu::analyzer::kShfAlloc | nwiiu::analyzer::kShfExec;
    jump_code.address = 0x02000000;
    jump_code.data = {
        0x3D, 0x80, 0x03, 0x00, // lis r12, 0x0300
        0x39, 0x8C, 0xFF, 0xF0, // addi r12, r12, -0x10
        0x7D, 0x8C, 0x18, 0x2E, // lwzx r12, r12, r3
        0x7D, 0x89, 0x03, 0xA6, // mtctr r12
        0x4E, 0x80, 0x04, 0x20, // bctr
        0x60, 0x00, 0x00, 0x00,
        0x60, 0x00, 0x00, 0x00,
        0x60, 0x00, 0x00, 0x00,
        0x4E, 0x80, 0x00, 0x20, // blr
        0x4E, 0x80, 0x00, 0x20, // blr
    };
    auto& jump_data = jump_table_image.sections.emplace_back();
    jump_data.name = ".rodata";
    jump_data.type = nwiiu::analyzer::kShtProgbits;
    jump_data.flags = nwiiu::analyzer::kShfAlloc;
    jump_data.address = 0x02FFFFF0;
    jump_data.data = {
        0x02, 0x00, 0x00, 0x20,
        0x02, 0x00, 0x00, 0x24,
        0x00, 0x00, 0x00, 0x00,
    };

    const auto jump_analysis =
        nwiiu::analyzer::analyze(jump_table_image);
    const auto& jump_function = jump_analysis.functions.at(0x02000000);
    test::require(
        jump_function.jump_table_targets ==
            std::set<uint32_t>{0x02000020, 0x02000024},
        "jump table target count");
    test::require(jump_function.instruction_count == 7,
                  "jump table target blocks decoded");
    test::require(
        jump_function.dynamic_transfers.size() == 3 &&
            jump_function.dynamic_transfers[0].address == 0x02000010 &&
            jump_function.dynamic_transfers[0].kind == "jump_table",
        "jump table transfer");
    test::require(jump_analysis.unresolved.empty(),
                  "jump table has no unresolved record");
    require_basic_blocks(
        jump_function, jump_table_image,
        {{0x02000000, 0x02000014, 5}, {0x02000020, 0x02000024, 1},
         {0x02000024, 0x02000028, 1}});

    auto conditional_jump_table_image = jump_table_image;
    conditional_jump_table_image.sections[0].data[16] = 0x4C;
    conditional_jump_table_image.sections[0].data[17] = 0x82; // bnectr
    conditional_jump_table_image.sections[0].data[20] = 0x41;
    conditional_jump_table_image.sections[0].data[21] = 0x80;
    conditional_jump_table_image.sections[0].data[22] = 0x00;
    conditional_jump_table_image.sections[0].data[23] = 0x08; // bc 0x0200001C
    const auto conditional_jump_analysis =
        nwiiu::analyzer::analyze(conditional_jump_table_image);
    const auto& conditional_jump_function =
        conditional_jump_analysis.functions.at(0x02000000);
    test::require(
        conditional_jump_function.jump_table_targets ==
            std::set<uint32_t>{0x02000020, 0x02000024},
        "conditional bcctr keeps taken jump-table edge");
    test::require(conditional_jump_function.instruction_count == 10,
                  "conditional bcctr keeps untaken fallthrough");
    test::require(conditional_jump_analysis.unresolved.empty(),
                  "conditional bcctr is fully classified");
    require_basic_blocks(
        conditional_jump_function, conditional_jump_table_image,
        {{0x02000000, 0x02000014, 5}, {0x02000014, 0x02000018, 1},
         {0x02000018, 0x0200001C, 1}, {0x0200001C, 0x02000020, 1},
         {0x02000020, 0x02000024, 1}, {0x02000024, 0x02000028, 1}});

    nwiiu::analyzer::RpxImage bare_bctr_image;
    bare_bctr_image.entry_point = 0x02000000;
    auto& bare_bctr_section = bare_bctr_image.sections.emplace_back();
    bare_bctr_section.type = nwiiu::analyzer::kShtProgbits;
    bare_bctr_section.flags =
        nwiiu::analyzer::kShfAlloc | nwiiu::analyzer::kShfExec;
    bare_bctr_section.address = 0x02000000;
    bare_bctr_section.data = {0x4E, 0x80, 0x04, 0x20};

    const auto bare_bctr_analysis =
        nwiiu::analyzer::analyze(bare_bctr_image);
    test::require(
        bare_bctr_analysis.unresolved.size() == 1 &&
            bare_bctr_analysis.unresolved[0].address == 0x02000000 &&
            bare_bctr_analysis.unresolved[0].category == "indirect_branch" &&
            bare_bctr_analysis.unresolved[0].reason ==
                "jump table base not recovered",
        "bare bctr unresolved");

    nwiiu::analyzer::RpxImage conditional_return_image;
    conditional_return_image.entry_point = 0x02000000;
    auto& conditional_return_section =
        conditional_return_image.sections.emplace_back();
    conditional_return_section.type = nwiiu::analyzer::kShtProgbits;
    conditional_return_section.flags =
        nwiiu::analyzer::kShfAlloc | nwiiu::analyzer::kShfExec;
    conditional_return_section.address = 0x02000000;
    conditional_return_section.data = {
        0x4C, 0x82, 0x00, 0x20, // bnelr
        0x4D, 0x82, 0x00, 0x20, // beqlr
        0x4E, 0x80, 0x00, 0x20, // blr
    };
    const auto conditional_return_analysis =
        nwiiu::analyzer::analyze(conditional_return_image);
    const auto& conditional_return_function =
        conditional_return_analysis.functions.at(0x02000000);
    test::require(conditional_return_function.instruction_count == 3,
                  "bnelr and beqlr keep fallthrough");
    test::require(conditional_return_function.dynamic_transfers.size() == 3,
                  "each conditional and unconditional return is recorded");
    test::require(conditional_return_analysis.unresolved.empty(),
                  "conditional returns are fully classified");

    auto boundary_image = jump_table_image;
    boundary_image.sections[0].data.insert(
        boundary_image.sections[0].data.end(),
        {0x4E, 0x80, 0x00, 0x20}); // blr
    boundary_image.sections[1].data = {
        0x02, 0x00, 0x00, 0x20,
        0x02, 0x00, 0x00, 0x28,
        0x02, 0x00, 0x00, 0x1C,
        0x00, 0x00, 0x00, 0x00,
    };
    boundary_image.relocations.emplace_back().target_address = 0x02000024;

    const auto boundary_analysis =
        nwiiu::analyzer::analyze(boundary_image);
    const auto& boundary_source =
        boundary_analysis.functions.at(0x02000000);
    const auto& boundary_neighbor =
        boundary_analysis.functions.at(0x02000024);
    test::require(
        boundary_source.jump_table_targets ==
            std::set<uint32_t>{0x02000020},
        "jump table keeps only root-owned targets");
    test::require(boundary_source.instruction_count == 6,
                  "jump table stops decoding at boundary");
    test::require(boundary_source.end <= boundary_neighbor.start,
                  "jump table does not overlap neighboring function");
    test::require(
        boundary_analysis.unresolved.size() == 1 &&
            boundary_analysis.unresolved[0].address == 0x02000010 &&
            boundary_analysis.unresolved[0].category ==
                "function_boundary" &&
            boundary_analysis.unresolved[0].reason ==
                "jump table target outside root-owned interval",
        "jump table boundary is unresolved");

    auto leading_boundary_image = boundary_image;
    leading_boundary_image.sections[1].data = {
        0x02, 0x00, 0x00, 0x28,
        0x02, 0x00, 0x00, 0x1C,
        0x00, 0x00, 0x00, 0x00,
    };
    const auto leading_boundary_analysis =
        nwiiu::analyzer::analyze(leading_boundary_image);
    const auto& leading_boundary_source =
        leading_boundary_analysis.functions.at(0x02000000);
    test::require(leading_boundary_source.jump_table_targets.empty() &&
                      leading_boundary_source.dynamic_transfers.empty(),
                  "boundary-only table is not a jump table");
    test::require(leading_boundary_source.instruction_count == 5,
                  "first boundary target stops table decoding");
    test::require(
        leading_boundary_analysis.unresolved.size() == 1 &&
            leading_boundary_analysis.unresolved[0].address == 0x02000010 &&
            leading_boundary_analysis.unresolved[0].category ==
                "function_boundary" &&
            leading_boundary_analysis.unresolved[0].reason ==
                "jump table target outside root-owned interval",
        "first jump table entry boundary is unresolved");

    nwiiu::analyzer::RpxImage split_image;
    split_image.entry_point = 0x02000000;
    auto& split_section = split_image.sections.emplace_back();
    split_section.type = nwiiu::analyzer::kShtProgbits;
    split_section.flags = nwiiu::analyzer::kShfExec;
    split_section.address = 0x02000000;
    const auto append_word = [&split_section](uint32_t word) {
        split_section.data.push_back(static_cast<uint8_t>(word >> 24));
        split_section.data.push_back(static_cast<uint8_t>(word >> 16));
        split_section.data.push_back(static_cast<uint8_t>(word >> 8));
        split_section.data.push_back(static_cast<uint8_t>(word));
    };
    append_word(0x60000000);
    append_word(0x60000000);
    append_word(0x4E800020);
    append_word(0x4E800020);
    append_word(0x4BFFFFF5);
    append_word(0x4E800020);
    split_image.relocations.emplace_back().target_address = 0x02000010;

    const auto split_analysis = nwiiu::analyzer::analyze(split_image);
    test::require(split_analysis.functions.size() == 3,
                  "late direct-call root function count");
    const auto& early = split_analysis.functions.at(0x02000000);
    const auto& split = split_analysis.functions.at(0x02000004);
    const auto& late = split_analysis.functions.at(0x02000010);
    test::require(early.instruction_count == 1 && early.end == split.start,
                  "earlier function stops at late-discovered root");
    test::require(split.instruction_count == 2 &&
                      split.discovery_reasons ==
                          std::set<std::string>{"direct_call"},
                  "late-discovered direct-call function");
    test::require(late.callees == std::set<uint32_t>{split.start} &&
                      split.callers == std::set<uint32_t>{late.start},
                  "late direct-call graph");
    test::require(early.end <= split.start && split.end <= late.start,
                  "functions do not overlap");

    nwiiu::analyzer::RpxImage overlap_image;
    overlap_image.entry_point = 0x02000000;
    auto& overlap_section = overlap_image.sections.emplace_back();
    overlap_section.type = nwiiu::analyzer::kShtProgbits;
    overlap_section.flags = nwiiu::analyzer::kShfExec;
    overlap_section.address = 0x02000000;
    overlap_section.data = {
        0x48, 0x00, 0x00, 0x08, // b 0x02000008
        0x48, 0x00, 0x00, 0x08, // b 0x0200000C
        0x60, 0x00, 0x00, 0x00, // nop
        0x4E, 0x80, 0x00, 0x20, // blr
    };
    overlap_image.relocations.emplace_back().target_address = 0x02000004;
    overlap_image.relocations.emplace_back().target_address = 0x02000008;

    // An inline branch table: bctr followed by a run of unconditional
    // branches, one per case. Nothing points at the entries statically, so
    // they are only reachable by recovering the table.
    nwiiu::analyzer::RpxImage table_image;
    table_image.entry_point = 0x02000000;
    auto& table_section = table_image.sections.emplace_back();
    table_section.type = nwiiu::analyzer::kShtProgbits;
    table_section.flags = nwiiu::analyzer::kShfExec;
    table_section.address = 0x02000000;
    table_section.data = {
        0x7C, 0x09, 0x03, 0xA6, // mtctr r0
        0x4E, 0x80, 0x04, 0x20, // bctr
        0x48, 0x00, 0x00, 0x0C, // b  -> 0x02000014
        0x48, 0x00, 0x00, 0x08, // b  -> 0x02000014
        0x48, 0x00, 0x00, 0x04, // b  -> 0x02000014
        0x4E, 0x80, 0x00, 0x20, // blr
    };
    const auto table_analysis = nwiiu::analyzer::analyze(table_image);
    for (uint32_t entry : {0x02000008u, 0x0200000Cu, 0x02000010u}) {
        const auto found = table_analysis.functions.find(entry);
        test::require(found != table_analysis.functions.end(),
                      "each inline branch-table entry is discovered");
        test::require(found->second.discovery_reasons.contains("branch_table"),
                      "branch-table entries record how they were found");
    }
    test::require(table_analysis.unresolved.empty(),
                  "a recovered inline branch table leaves nothing unresolved");

    const auto overlap_analysis = nwiiu::analyzer::analyze(overlap_image);
    // A branch target outside the current interval is now itself a root, so
    // 0x0200000C becomes a fourth function instead of being dropped.
    test::require(overlap_analysis.functions.size() == 4,
                  "branch-around function count");
    const auto& first = overlap_analysis.functions.at(0x02000000);
    const auto& neighbor = overlap_analysis.functions.at(0x02000004);
    const auto& last = overlap_analysis.functions.at(0x02000008);
    test::require(first.instruction_count == 1 && first.end == neighbor.start,
                  "branch-around first function ownership");
    test::require(neighbor.instruction_count == 1 &&
                      neighbor.end == last.start,
                  "branch-around neighbor ownership");
    test::require(last.instruction_count == 1 &&
                      last.end == 0x0200000C,
                  "branch-around last function ownership");
    const auto& discovered = overlap_analysis.functions.at(0x0200000C);
    test::require(discovered.discovery_reasons ==
                      std::set<std::string>{"direct_branch"},
                  "a branch target outside every interval is discovered as a "
                  "root rather than dropped");
    test::require(discovered.instruction_count == 1 &&
                      discovered.end == 0x02000010,
                  "discovered branch target owns its own instructions");
    test::require(first.instruction_count + neighbor.instruction_count +
                          last.instruction_count + discovered.instruction_count ==
                      4 &&
                      first.end <= neighbor.start &&
                      neighbor.end <= last.start,
                  "branch-around functions are disjoint");
    test::require(first.callees == std::set<uint32_t>{last.start} &&
                      last.callers == std::set<uint32_t>{first.start},
                  "known boundary target is a tail edge");
    test::require(overlap_analysis.unresolved.empty(),
                  "a direct branch target is statically known, so nothing is "
                  "left unresolved");
}
