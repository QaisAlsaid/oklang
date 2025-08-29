#ifndef OK_VM_STACK_HPP
#define OK_VM_STACK_HPP

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
} // namespace ok

#endif // OK_VM_STACK_HPP