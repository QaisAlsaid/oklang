#include "vm_stack.hpp"
#include "vm.hpp"
#include <cassert>

namespace ok
{
  static thread_local std::vector<vm*> g_vms;

  vm* get_g_vm()
  {
    // you cant access nullptr you silly!
    assert(!g_vms.empty());
    return g_vms.back();
  }

  vm_guard::vm_guard(vm* p_new_g_vm)
  {
    g_vms.push_back(p_new_g_vm);
  }

  vm_guard::~vm_guard()
  {
    g_vms.pop_back();
  }

  logger& get_vm_logger()
  {
    return get_g_vm()->get_logger();
  }

  gc& get_vm_gc()
  {
    return get_g_vm()->get_gc();
  }
} // namespace ok