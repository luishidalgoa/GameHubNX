// Pure rules of the updates tab: which DLC a console still lacks, and what a row
// has pending. The screen code only shows what these return.
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

#include "app/addon_status.hpp"

using gamehubnx::RowAddOnKind;
using gamehubnx::classifyRowAddOns;
using gamehubnx::missingDlcTitleIds;

int main() {
    // The catalog lists three DLC; the console has one of them.
    {
        const auto missing = missingDlcTitleIds(
            {"0100A7C01B793001", "0100A7C01B793002", "0100A7C01B793003"},
            {"0100A7C01B793002"});
        assert((missing == std::vector<std::string>{"0100A7C01B793001", "0100A7C01B793003"}));
    }
    // An installed DLC that the catalog does not list is not a missing one.
    {
        const auto missing = missingDlcTitleIds({"0100A7C01B793001"},
                                                {"0100A7C01B79FFFF"});
        assert((missing == std::vector<std::string>{"0100A7C01B793001"}));
    }
    // Everything installed: nothing is missing.
    {
        assert(missingDlcTitleIds({"A", "B"}, {"B", "A"}).empty());
    }
    // A DLC repeated in the catalog is offered once.
    {
        assert((missingDlcTitleIds({"A", "A", "B"}, {}) ==
                std::vector<std::string>{"A", "B"}));
    }
    // Row kinds: update, DLC, both, none.
    assert(classifyRowAddOns(true, false) == RowAddOnKind::Update);
    assert(classifyRowAddOns(false, true) == RowAddOnKind::Dlc);
    assert(classifyRowAddOns(true, true) == RowAddOnKind::UpdateAndDlc);
    assert(classifyRowAddOns(false, false) == RowAddOnKind::None);

    std::puts("test_addon_status: ok");
    return 0;
}
