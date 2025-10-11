#include "gc.hpp"
#include "macros.hpp"
#include "object.hpp"
#include "value.hpp"
#include "vm.hpp"
#include "vm_stack.hpp"
#include <unordered_map>

namespace ok
{
  void gc::increment_used_memory(size_t p_by)
  {
    m_used_memory += p_by;
#ifndef OK_STRESS_GC
    if(m_used_memory > m_next)
#endif
    {
      collect();
    }
  }

  void gc::collect()
  {
#if defined(OK_LOG_GC)
    auto before = m_used_memory;
    TRACELN("[start gc]");
#endif
    auto _vm = get_g_vm();
    mark_roots();
    trace_references();
    remove_ghost_references(_vm->m_interned_strings.underlying());
    sweep();
    m_next = m_used_memory * s_grow_factor;

#if defined(OK_LOG_GC)
    TRACELN("collected: {} bytes (from {} to {}) next at {}", before - m_used_memory, before, m_used_memory, m_next);
    TRACELN("[end gc]");
#endif
  }

  void gc::mark_roots()
  {
    auto _vm = get_g_vm();
    for(auto val : _vm->m_stack)
    {
      mark_value(val);
    }
    for(auto& frame : _vm->m_call_frames)
    {
      mark_object((object*)frame.closure);
    }
    for(auto up = _vm->m_open_upvalues; up != nullptr; up = up->next)
    {
      mark_object((object*)up);
    }
    for(auto val : m_keep)
    {
      mark_value(val);
    }
    mark_hashtable(_vm->m_globals);
    mark_compiler_roots();
    auto& vm_statics = _vm->get_statics();
    mark_object((object*)vm_statics.init_string);
    mark_object((object*)vm_statics.deinit_string);
  }

  void gc::mark_compiler_roots()
  {
    auto _vm = get_g_vm();
    for(auto& ctx : _vm->m_compiler.m_function_contexts)
    {
      mark_object((object*)ctx.function.function);
    }
    for(auto pair : _vm->m_compiler.m_globals)
    {
      mark_object((object*)pair.first);
    }
  }

  void gc::trace_references()
  {
    for(long i = m_gray.size(); i-- > 0;)
    {
      auto obj = m_gray.back();
      m_gray.pop_back();
      trace_object_references(obj);
    }
  }

  void gc::mark_value(value_t p_value)
  {
#if defined(OK_LOG_GC)
    TRACE("mark_value: ");
    auto _vm = get_g_vm();
    _vm->print_value(p_value);
    TRACELN("");
#endif
    if(p_value.type == value_type::object_val)
      mark_object(p_value.as.obj);
  }

  void gc::mark_object(object* p_object)
  {
    if(p_object == nullptr || p_object->is_marked())
      return;
#if defined(OK_LOG_GC)
    TRACELN("mark_object: {:p}", (void*)p_object);
#endif
    p_object->set_marked(true);
    m_gray.push_back(p_object);
  }

  void gc::mark_hashtable(const std::unordered_map<string_object*, value_t>& p_table)
  {
    for(auto pair : p_table)
    {
      mark_object((object*)pair.first);
      mark_value(pair.second);
    }
  }

  void gc::trace_object_references(object* p_object)
  {
    auto _vm = get_g_vm();
#if defined(OK_LOG_GC)
    TRACE("trace_object_references: {:p} ", (void*)p_object);
    _vm->print_object(p_object);
    TRACELN("");
#endif
    switch(p_object->get_type())
    {
    case object_type::obj_string:
    case object_type::obj_native_function:
      break;
    case object_type::obj_upvalue:
    {
      mark_value(((upvalue_object*)p_object)->closed);
      break;
    }
    case object_type::obj_function:
    {
      auto fu = (function_object*)p_object;
      mark_object((object*)fu->name);
      mark_chunk(fu->associated_chunk);
      break;
    }
    case object_type::obj_closure:
    {
      auto closure = (closure_object*)p_object;
      mark_object((object*)closure->function);
      for(auto up : closure->upvalues)
      {
        mark_object((object*)up);
      }
      break;
    }
    case object_type::obj_class:
    {
      auto class_ = (class_object*)p_object;
      mark_object((object*)class_->name);
      mark_hashtable(class_->methods);
      break;
    }
    case object_type::obj_instance:
    {
      auto instance = (instance_object*)p_object;
      mark_object((object*)instance->class_);
      mark_hashtable(instance->fields);
      break;
    }
    case object_type::obj_bound_method:
    {
      auto bm = (bound_method_object*)p_object;
      mark_value(bm->receiver);
      mark_object((object*)bm->method);
      break;
    }
    }
  }

  void gc::mark_chunk(chunk& p_chunk)
  {
    mark_array(p_chunk.constants);
    mark_array(p_chunk.identifiers);
  }

  void gc::mark_array(value_array& p_array)
  {
    for(auto elem : p_array)
    {
      mark_value(elem);
    }
  }

  void gc::remove_ghost_references(std::unordered_map<hashed_string, string_object*>& p_table)
  {
    for(auto it = p_table.begin(); it != p_table.end();)
    {
      if(!it->second->up.is_marked())
      {
        it = p_table.erase(it);
      }
      else
      {
        ++it;
      }
    }
  }

  void gc::sweep()
  {
    auto _vm = get_g_vm();
    object* prev = nullptr;
    object* obj = _vm->m_objects_list;
    while(obj != nullptr)
    {
      if(obj->is_marked())
      {
        obj->set_marked(false);
        prev = obj;
        obj = obj->next;
      }
      else
      {
        auto* unreached = obj;
        obj = obj->next;
        if(prev == nullptr)
        {
          _vm->m_objects_list = obj;
        }
        else
        {
          prev->next = obj;
        }
        delete_object(unreached);
      }
    }
  }
} // namespace ok