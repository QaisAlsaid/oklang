#ifndef OK_VM_STACK_HPP
#define OK_VM_STACK_HPP

#include "gc.hpp"
#include "log.hpp"
namespace ok
{
  class vm;

  vm* get_g_vm();

  class vm_guard
  {
  public:
    vm_guard(vm* p_new_g_vm);
    ~vm_guard();
  };

  // just helper instead of doing get_g_vm().get_logger(), it also abstracts the vm interaction away, this way you dont
  // have to include the vm.hpp file to access the vm logger
  logger& get_vm_logger();

  gc& get_vm_gc();
} // namespace ok

#endif // OK_VM_STACK_HPP