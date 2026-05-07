#include "core_types/typed_ids.hpp"

int main() {
  yac::ApprovalId a{"test"};
  yac::ToolCallId b = a;
  return 0;
}
