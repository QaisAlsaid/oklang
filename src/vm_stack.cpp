#include "vm_stack.hpp"
#include "vm.hpp"
#include <cassert>

namespace ok
{
  static thread_local vm* g_vm;

  vm* get_g_vm()
  {
    // you cant access nullptr you silly!
    assert(g_vm != nullptr);
    return g_vm;
  }

  vm_guard::vm_guard(vm* p_new_g_vm) : m_prev_g_vm(g_vm)
  {
    g_vm = p_new_g_vm;
  }

  vm_guard::~vm_guard()
  {
    g_vm = m_prev_g_vm;
  }
} // namespace ok