#pragma once

#include "sa/common/opaque_id.hpp"

namespace sa::common {

// Identity of one process generation or one gateway session. A restarted process is a new
// instance even under the same pod name (see README "소유권과 lease").
using InstanceId = OpaqueId<struct InstanceIdTag>;

}  // namespace sa::common
