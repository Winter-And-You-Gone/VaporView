#include "ground/main/UpdateCheckStatus.h"

#include <cstdlib>
#include <iostream>

namespace
{

void require(bool condition, const char *message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

} // namespace

int main()
{
    require(VaporView::ifwUpdateCommandSucceeded(0),
            "IFW success exit code is accepted");
    require(VaporView::ifwUpdateCommandSucceeded(
                VaporView::kIfwEssentialComponentsUpdatedExitCode),
            "IFW essential-component restart exit code is accepted");
    require(!VaporView::ifwUpdateCommandSucceeded(3),
            "IFW no-update exit code is not treated as an update success");
    require(!VaporView::ifwUpdateCommandSucceeded(1),
            "IFW failure exit code is rejected");

    std::cout << "ifw_update_status_test passed\n";
    return 0;
}
